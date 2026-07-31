# ARCHITECTURE.md — CertMind ESP32-S3 Client

Documento de arquitetura interna. Para instalação, montagem e comportamento dos LEDs, veja o
**[README.md](README.md)**.

## 1. Visão Geral

Firmware **PlatformIO** para **ESP32-S3 Super Mini** que consome um stream **SSE** (Server-Sent Events)
da API CertMind e aciona os pixels de uma **barra WS2812 de 8 LEDs** (GPIO 13) conforme cada
situação emitida pelo backend. No M1 do porte, apenas os pixels 0–4 (posições A–E) são usados —
comportamento idêntico ao firmware v2.5 do D1 Mini.

| | |
|---|---|
| **Plataforma** | ESP32-S3 Super Mini (ESP32-S3FH4R2: 4 MB flash quad + 2 MB PSRAM quad) — framework Arduino |
| **Versão** | 3.0 |
| **Environment** | `[env:esp32s3_supermini]` (board `esp32-s3-devkitc-1` com overrides de flash/PSRAM) |
| **Dependências** | ArduinoJson `^6.21.5`, FastLED `3.9.13` |
| **Protocolo** | HTTP/1.1 GET → SSE (texto claro, sem TLS) |
| **Transporte do corpo** | `Transfer-Encoding: chunked` (Kestrel), desmontado no firmware |
| **Eventos consumidos** | `answer` (resposta resolvida), `status` (andamento do processamento) |

O ESP é **apenas consumidor**: abre o GET e escuta. Não faz POST nem envia imagem.

---

## 2. Estrutura do Projeto

```
Esp8266_Cert_Assistant/
├── src/
│   ├── Config.h                # Constantes centralizadas (#define)
│   ├── main.cpp                # Orquestrador: setup(), loop(), handleAnswer(), handleStatus()
│   ├── WiFiManager.h/.cpp      # Conexão WiFi (modo STA, modem sleep desligado)
│   ├── SseClient.h/.cpp        # Cliente SSE: TCP, de-framing chunked, parser, reconexão
│   └── LedController.h/.cpp    # Máquina de estados dos LEDs (não-bloqueante, saída FastLED/WS2812)
├── platformio.ini              # Configuração PlatformIO
├── ARCHITECTURE.md             # Este documento
├── README.md                   # Documentação de uso do projeto
├── CLAUDE.md                   # Diretrizes para assistentes de IA
├── INSTALL_ARDUINOJSON.md      # Nota sobre a dependência ArduinoJson
└── .vscode/                    # Configurações do editor (não versionadas)
```

---

## 3. Diagrama de Componentes

```
┌──────────────────────────────────────────────────────────────┐
│                          main.cpp                            │
│                                                              │
│  setup():                                                    │
│    leds.begin()                                              │
│    monta g_filter (ArduinoJson)                              │
│    wifiManager.connect()                                     │
│    sse.begin(handleAnswer, handleStatus)                     │
│                                                              │
│  loop():                                                     │
│    leds.update()                                             │
│    sse.loop()                                                │
│    leds.setConnected(sse.isStreaming())                      │
│    log periódico do heap                                     │
│                                                              │
│  handleAnswer(char* payload):  deserializeJson → padrão LED  │
│  handleStatus(char* payload):  deserializeJson → padrão LED  │
└───────┬──────────────────┬───────────────────┬───────────────┘
        │                  │                   │
┌───────▼───────┐  ┌───────▼────────────┐  ┌───▼───────────────┐
│  WiFiManager  │  │    LedController   │  │     SseClient     │
│               │  │                    │  │                   │
│ • connect()   │  │ • showSingle()     │  │ • begin(cbA, cbS) │
│ • isConnected │  │ • showMultiple()   │  │ • loop()          │
│ • getIP()     │  │ • showYesNo()      │  │ • isStreaming()   │
│ • reconnect() │  │ • showSlots()      │  │                   │
└───────────────┘  │ • showTestChase()  │  │ Estados:          │
                   │ • showError()      │  │  ST_DISCONNECTED  │
                   │ • showProcessing() │  │  ST_HEADERS       │
                   │ • stopProcessing() │  │  ST_STREAMING     │
                   │ • setConnected()   │  │                   │
                   │ • update()         │  │ De-framing:       │
                   └────────────────────┘  │  CHUNK_SIZE       │
                                           │  CHUNK_DATA       │
                                           │  CHUNK_CRLF       │
                                           └───────────────────┘
```

---

## 4. Fluxo de Dados

```
CertMind Backend (Kestrel)
    │
    │  TCP GET /api/exam/stream HTTP/1.1
    │  Connection: keep-alive · Accept: text/event-stream
    │
    ▼
SseClient::tryConnect()
    │   Valida headers (200 + text/event-stream) e detecta Transfer-Encoding: chunked
    │   TODO header é logado ("[SSE] < ...")  →  state = ST_STREAMING
    ▼
SseClient::pump()                       [bytes disponíveis, não-bloqueante]
    │
    ├── _chunked → feedChunkedByte()    consome <size-hex>, dados, CRLF terminador
    │                                   e entrega a feedLine() só o corpo lógico
    └── identity → feedLine()           caminho direto
    │
    ▼
SseClient::processSseLine()             event: · data: · ":" comment (keepalive)
    │
    ▼
SseClient::dispatchEvent()              linha em branco = fim do evento
    │
    ├── EVT_ANSWER → handleAnswer(char* payload)
    ├── EVT_STATUS → handleStatus(char* payload)
    └── EVT_NONE   → descartado em silêncio
    │
    ▼
main.cpp: deserializeJson(g_doc, payload, Filter(g_filter))   [zero-copy]
    │
    ├─ answer ──┬── questionType == "test"          → leds.showTestChase()
    │           ├── hasData == false                → leds.showError()
    │           ├── "single"                        → leds.showSingle()
    │           ├── "multiple"                      → leds.showMultiple()
    │           ├── "yesno"                         → leds.showYesNo()
    │           ├── "dropdown"/"ordering"/"matching"→ leds.showSlots()
    │           ├── desconhecido                    → leds.showError()
    │           └── erro de parse                   → leds.showError()
    │
    └─ status ──┬── "solving"                       → leds.showProcessing()
                ├── "error"                         → leds.showError()
                ├── "idle" / desconhecido           → leds.stopProcessing()
                └── erro de parse                   → só log (sem showError)
    │
    ▼
LedController::update()
    │
    ├── Resposta ativa → renderiza o modo atual (HOLD / CHASE / ERROR / SEQ / PROCESSING)
    └── Sem resposta   → janela de boot expirada e 1ª resposta ainda não chegou?
                         ├── sim → blackout (allOff())
                         └── não → conexão (A+E piscando) ou ocioso (pulso em A)
```

---

## 5. Detalhamento dos Módulos

### 5.1. `Config.h`

Centraliza **todas** as constantes via `#define`. Nenhuma configuração espalhada pelo código.

| Categoria | Constantes | Valores |
|---|---|---|
| WiFi | `WIFI_SSID`, `WIFI_PASSWORD`, `WIFI_MAX_RETRY_ATTEMPTS`, `WIFI_RETRY_DELAY_MS` | —, —, 30, 500 ms |
| Stream | `STREAM_HOST`, `STREAM_PORT`, `STREAM_PATH`, `STREAM_TIMEOUT_MS`, `STREAM_HEAP_LOG_MS` | `192.168.15.38`, 8090, `/api/exam/stream`, 40 s, 10 s |
| Backoff | `STREAM_BACKOFF_TABLE` | `{1000, 2000, 5000, 10000, 20000, 30000}` ms |
| Buffers | `SSE_MAX_LINE`, `SSE_MAX_DATA`, `SSE_MAX_CHUNK` | 4096, 4096, 65535 |
| JSON | `JSON_DOC_SIZE`, `JSON_FILTER_SIZE` | 512, 256 bytes |
| Barra de LED | `LED_BAR_PIN`, `LED_BAR_COUNT`, `LED_COUNT`, `LED_BRIGHTNESS`, `LED_MAX_VOLTS`/`LED_MAX_MILLIAMPS`, `LED_COLOR_A`..`LED_COLOR_F` | GPIO 13, 8, 5, 96, 5 V/600 mA, cores do D1 |
| Conexão / ocioso | `LED_CONN_BLINK_MS`, `LED_IDLE_PERIOD_MS`, `LED_IDLE_PULSE_MS` | 150 ms, 2000 ms, 80 ms |
| Janela de boot | `LED_BOOT_BLINK_MS` | 300000 ms (5 min) |
| HOLD | `LED_HOLD_TTL_MS`, `LED_HOLD_INTAKE_MS`, `LED_YESNO_BLINK_MS` | 12000 ms, 250 ms, 350 ms |
| Processando | `LED_PROC_INDEX`, `LED_PROC_BLINK_MS` | 2 (LED C), 250 ms |
| Test / erro | `LED_CHASE_STEP_MS`, `LED_CHASE_PASSES`, `LED_ERROR_ON_MS`, `LED_ERROR_OFF_MS`, `LED_ERROR_CYCLES` | 120 ms, 2, 250 ms, 250 ms, 3 |
| Sequências | `LED_SEQ_STEP_MS`, `LED_SEQ_GAP_MS`, `LED_SEQ_BLINK_MS`, `LED_SEQ_PASSES`, `LED_SEQ_ERRBLINK_MS` | 1500 ms, 400 ms, 200 ms, 2, 250 ms |
| Serial | `SERIAL_BAUD_RATE` | 115200 |

### 5.2. `WiFiManager`

Responsabilidade: conexão WiFi no modo STA (Station).

- **`connect()`** — conecta com até 30 tentativas. **Bloqueante**; usado apenas na inicialização.
- **`isConnected()`** — status da conexão.
- **`getIP()`** — IP atribuído.
- **`reconnect()`** — tenta reconexão (usado internamente pelo `SseClient`).

Após o boot, `WiFi.setAutoReconnect(true)` cuida das quedas de rede.

> **Nota:** as credenciais WiFi ficam em texto plano no código, versionadas, por decisão do mantenedor.

### 5.3. `SseClient`

Responsabilidade: gerenciar a conexão SSE ponta-a-ponta.

**Máquina de estados da conexão:**
```
ST_DISCONNECTED ──▶ ST_HEADERS ──▶ ST_STREAMING
        ▲                │                │
        └────────────────┴────────────────┘
              (falha / timeout / socket ou WiFi caiu)
```

**Máquina de estados do de-framing chunked:**
```
CHUNK_SIZE ──▶ CHUNK_DATA ──▶ CHUNK_CRLF ──┐
     ▲                                      │
     └──────────────────────────────────────┘
```

**Componentes internos:**

| Método | Função |
|---|---|
| `tryConnect()` | Abre TCP e envia o GET montado à mão (sem `HTTPClient`) |
| `pump(headerPhase)` | Lê os bytes disponíveis, não-bloqueante |
| `feedChunkedByte(c)` | Desmonta o frame chunked; só os bytes de dados seguem para `feedLine()` |
| `beginChunkSize()` | Rearma a leitura de um chunk-size |
| `feedLine(c, headerPhase)` | Monta a linha lógica a partir dos bytes do corpo |
| `processHeaderLine()` | Valida `HTTP/1.1 200` e `Content-Type`; detecta `Transfer-Encoding: chunked`; **loga todo header** |
| `processSseLine()` | Parser do protocolo SSE (`event:`, `data:`, `:` comentários) |
| `dispatchEvent()` | Na linha em branco, dispara o callback de `answer` ou `status` |
| `scheduleRetry()` | Backoff progressivo com a tabela do `Config.h` |
| `checkTimeout()` | ~40 s sem nenhuma linha → reconecta |

**Dados internos:**

| Campo | Tamanho | Função |
|---|---|---|
| `_lineBuf` | 4096 B | Linha atual (compartilhado entre header e stream) |
| `_dataBuf` | 4096 B | Payload acumulado dos campos `data:` — **mutável**, entregue ao callback |
| `_evtType` | enum | `EVT_NONE` / `EVT_ANSWER` / `EVT_STATUS` |
| `_chunked` | bool | Detectado nos headers; escolhe o caminho de de-framing |
| `_chunkState`, `_chunkRemaining`, `_chunkInExt`, `_chunkHasDigits` | — | Estado do de-framing |

### 5.4. `LedController`

Responsabilidade: máquina de estados dos 5 LEDs com animações não-bloqueantes.

**Modos de exibição:**

| Modo | Trigger | Comportamento | Fim |
|---|---|---|---|
| **Conectando** | `setConnected(false)` | A+E piscam juntos (150 ms) | — |
| **Ocioso** | `setConnected(true)` | Pulso curto em A (80 ms a cada 2 s) | — |
| **Blackout** | janela de boot expirada, sem 1ª resposta | Todos apagados | 1ª resposta (definitivo) |
| `MODE_HOLD` | `showSingle()` / `showMultiple()` / `showYesNo()` | LEDs fixos (yesno: "Não" piscando) | TTL de 12 s |
| `MODE_CHASE` | `showTestChase()` | Varredura A→E, 2 passadas de 120 ms/passo | Fim da animação |
| `MODE_ERROR` | `showError()` | Os 5 piscam juntos 3× (250 ms on/off) | Fim da animação |
| `MODE_SEQ` | `showSlots()` | Passo a passo com gaps, 2 passadas | Fim da animação |
| `MODE_PROCESSING` | `showProcessing()` | LED C piscando a 250 ms, **sem TTL** | `answer` / `error` / `idle` / queda do stream |

**Prioridade de exibição:**

1. Resposta tem prioridade sobre conexão.
2. Um `answer` novo sempre interrompe a exibição atual.
3. `test` / erro / sequências tocam e voltam ao ocioso.
4. `single` / `multiple` / `yesno` ficam retidos por `LED_HOLD_TTL_MS` e voltam ao heartbeat.
5. Conexão caindo/reconectando vence o estado ocioso.
6. "Processando" vence resposta segurada e dura até chegar `answer` / `error` / `idle`.

**Propriedades importantes:**

- **100% não-bloqueante** — toda temporização em `millis()`, zero `delay()`.
- **Blank de intake** (250 ms) — todo HOLD começa apagado, o que torna visível a chegada de
  respostas iguais consecutivas (ex.: `A` depois `A`).
- **`yesno` simultâneo** — cada afirmação acende seu LED ao mesmo tempo: Sim fixo, Não piscando.
- **`stopProcessing()` é condicional** — só age se o modo corrente é `MODE_PROCESSING`. É isso que
  impede o `idle` (que chega logo após o `answer`) de apagar a resposta exibida.
- **`showProcessing()` é idempotente** — se já está em `MODE_PROCESSING`, não reinicia a animação.
- **Queda do stream aborta o "processando"** — `setConnected(false)` derruba `MODE_PROCESSING`
  porque ele não tem TTL; sem isso o LED C piscaria para sempre escondendo a perda do stream.
- **Janela de boot** (5 min) — LEDs sinalizam normalmente e depois entram em blackout até a
  1ª resposta; o gate só roda quando não há resposta ativa.

**Estados internos:**

```
_answerActive (bool)        → há resposta/processando sendo exibido
_mode (enum Mode)           → HOLD / CHASE / ERROR / SEQ / PROCESSING
_connected (bool)           → saúde do stream (só usada quando !_answerActive)
_animStart (unsigned long)  → millis() do início da animação (TTL, blank, passos)
_bootMillis                 → millis() do begin(): início da janela de boot
_firstAnswerReceived        → encerra o blackout em definitivo (não rearma)
_blackoutAnnounced          → log one-shot ao entrar em blackout
_holdMask / _holdBlinkMask  → máscaras de LEDs fixos / piscando no HOLD
_stepType / _stepLed / _stepCount → buffer da sequência corrente
```

### 5.5. `main.cpp`

Responsabilidade: orquestração e parsing JSON.

- **`setup()`** — Serial, LEDs, filtro do ArduinoJson, WiFi e SSE (com os dois callbacks).
- **`loop()`** — `leds.update()`, `sse.loop()`, `leds.setConnected(...)`, log de heap, `yield()`.
- **`handleAnswer(char* payload)`** — decide o padrão por `hasData` + `questionType`.
- **`handleStatus(char* payload)`** — decide o padrão por `state`.

**Parsing JSON:**

- `StaticJsonDocument<512>` (`g_doc`) para os dados e `StaticJsonDocument<256>` (`g_filter`) para o
  filtro — **globais**, para não estourar a pilha do `loop()`.
- Um único filtro serve aos dois eventos: chaves ausentes no payload são simplesmente ignoradas.
  Ele lista `hasData`, `questionType`, `letters`, `flags`, `slots`, `slotCount`, `answerText`,
  `state` e `activeSolves` — e **omite `explanation`**, o campo mais longo.
- **Zero-copy** via buffer mutável (`char*`) → ativa o `StringMover` do ArduinoJson (~160/512 bytes
  de pool, contra ~507/512 quando as strings são copiadas).
- Letras são normalizadas com `toupper()` e validadas em A–E; arrays são truncados em `LED_COUNT`
  com aviso na serial.

---

## 6. Protocolo de Comunicação

### 6.1. Transporte

| Camada | Detalhes |
|---|---|
| Rede | TCP/IP via `WiFiClient` (socket raw) |
| Segurança | **Nenhuma** — HTTP puro, sem TLS/SSL |
| Aplicação | HTTP/1.1 GET, `Connection: keep-alive` |
| Corpo | `Transfer-Encoding: chunked` (Kestrel) — desmontado por `feedChunkedByte()`; `identity` também suportado |

### 6.2. Request HTTP

```http
GET /api/exam/stream HTTP/1.1
Host: 192.168.15.38:8090
Connection: keep-alive
Accept: text/event-stream
```

### 6.3. SSE (Server-Sent Events)

Formato padrão WHATWG — corpo **lógico**, após o de-framing:

```
: connected                     ← comentário (abertura)

event: status                   ← tipo do evento
data: {"state":"solving",...}   ← payload JSON
                                ← linha em branco = fim do evento

event: answer
data: {"hasData":true,...}

: ping                          ← keepalive a cada 15 s
```

O que chega **na conexão**, porém, é o corpo enquadrado:

```
2a\r\n                          ← chunk-size em hex (+ extensões opcionais)
event: answer\ndata: {...}\n\n  ← 0x2a bytes de dados
\r\n                            ← CRLF terminador do chunk
```

Sem o de-framing, o chunk-size viraria uma linha solta e o CRLF terminador uma linha vazia espúria —
que encerra o evento antes da hora sempre que um chunk termina no meio de uma linha `data:`.

### 6.4. Payload do evento `answer` (`SolverOutput`)

```json
{
  "hasData": true,
  "questionType": "single|multiple|yesno|dropdown|ordering|matching|test",
  "letters": ["A", "C"],
  "flags": [true, false, true],
  "slots": [2, 4, 1],
  "slotCount": 3,
  "answerText": "...",
  "explanation": "...",
  "elapsedMilliseconds": 4212
}
```

`letters` só é lida em `single`/`multiple`; `flags` em `yesno`; `slots` (posições 1–5) em
`dropdown`/`ordering`/`matching`. `explanation` e `elapsedMilliseconds` não são parseados.

### 6.5. Payload do evento `status`

```json
{ "state": "solving|idle|error", "activeSolves": 1 }
```

Sequência normal de um solve: `status solving` → `answer` → `status idle`.
Em falha: `status solving` → `status error` (**sem** `answer`).
Ao abrir a conexão o servidor manda um `status` com o estado atual, então reconectar no meio de um
processamento já traz `solving` — é assim que o estado ressincroniza.

---

## 7. Mapeamento de Pinos

Um único pino de dados: **GPIO 13 → DIN da barra WS2812 de 8 pixels** (FastLED sobre RMT,
ordem de cor GRB). O mapeamento lógico fica nos pixels:

| Pixel | Cor (`LED_COLOR_*`) | Função |
|---|---|---|
| 0 | Verde    | LED A (posição 1) |
| 1 | Amarelo  | LED B (posição 2) |
| 2 | Vermelho | LED C (posição 3) — também o LED de "processando" |
| 3 | Azul     | LED D (posição 4) |
| 4 | Branco   | LED E (posição 5) |
| 5–7 | — | Apagados; reservados ao M3 (posição F = magenta + 2 pixels de status) |

Pinos a **evitar** no S3 em expansões futuras: GPIO 0 (strapping/BOOT), 19–20 (USB D-/D+),
26–32 (flash SPI), 45–46 (strapping), 48 (WS2812 onboard).

---

## 8. Temporização e Non-Blocking

Todo o caminho de renderização opera sem `delay()`. As exceções são deliberadas: um `delay(100)`
único no `setup()` (para a Serial subir) e o tick de `delay(1)` no fim do `loop()` e entre linhas
do parser — sob FreeRTOS, `yield()` não alimenta o WDT nem cede o tick; `delay(1)` faz as duas
coisas (era `yield()` no ESP8266).

| Evento | Duração | Descrição |
|---|---|---|
| Blink de conexão | 150 ms | A+E piscam ao conectar/reconectar |
| Pulso ocioso | 80 ms a cada 2 s | Heartbeat em A |
| Janela de boot | 5 min | LEDs sinalizam antes do blackout |
| Processando | 250 ms on/off | LED C piscando, sem TTL |
| Chase (passo) | 120 ms | 2 passadas nos 5 LEDs |
| Erro | 250 ms on/off × 3 | Os 5 LEDs juntos |
| Sequência (passo) | 1500 ms + gap de 400 ms | 2 passadas |
| HOLD TTL | 12 s | Resposta exibida antes de voltar ao ocioso |
| HOLD intake | 250 ms | Blank no início de cada resposta |
| Yes/No blink | 350 ms | LED de "Não" piscando |
| Timeout do stream | 40 s | Sem nenhuma linha → reconecta (> 2× o ping de 15 s) |
| Log de heap | 10 s | `[HEAP] livre=` |

---

## 9. Decisões de Design Críticas

| Decisão | Motivação |
|---|---|
| **Sem `HTTPClient`** | Bloqueia esperando o corpo terminar — incompatível com stream SSE infinito |
| **Sem `WiFiClientSecure`** | O backend não tem HTTPS; TLS é o M7 do plano de migração (bloqueado pelo backend) |
| **`WiFi.setSleep(false)`** | O modem sleep do ESP32 (ligado por padrão) injeta dezenas a centenas de ms de jitter na recepção do stream |
| **`FastLED.show()` só quando a máscara muda** | O `update()` roda a cada iteração do loop; retransmitir o barramento WS2812 continuamente seria desperdício |
| **Cores em hex cru no `Config.h`** | `LED_COLOR_*` como `0xRRGGBB` evita que todo mundo que inclui `Config.h` dependa do FastLED |
| **De-framing chunked próprio** | O corpo na conexão não é o corpo lógico; sem desmontar, um chunk que termina no meio de uma linha `data:` trunca o payload (v2.3) |
| **Todo header logado** | Foi a ausência desse log que manteve a falta do de-framing invisível (v2.3) |
| **Zero-copy JSON** | Buffer mutável (`char*`) ativa o `StringMover` do ArduinoJson (~160/512 bytes vs. ~507/512 com cópia) |
| **Filtro JSON** | Ignora `explanation` (campo mais longo) para caber em 512 bytes |
| **Erro visível no `answer`** | `showError()` em vez de `return` silencioso — o silêncio era o que escondia bugs (v2.1) |
| **Erro *invisível* no `status`** | Piscar os 5 LEDs num status corrompido seria lido como "questão ilegível"; fica só no log |
| **`stopProcessing()` condicional** | O `idle` chega logo após o `answer`; um `allOff()` incondicional apagaria a resposta |
| **Processando sem TTL, abortado na queda** | O andamento pode demorar; mas sem TTL, uma queda de stream deixaria o LED piscando para sempre |
| **Blackout pós-boot** | Evita poluição luminosa em uso prolongado sem eventos; encerra em definitivo na 1ª resposta |
| **Chunk-size sem dígito hex apenas rearma** | Tratá-lo como tamanho 0 dispararia um retry extra e pularia um degrau do backoff |
| **Todo `millis()`** | Responsividade máxima; o `loop()` roda a milhares de Hz |
| **Backoff progressivo** | Evita martelar o backend com reconexões imediatas |

---

## 10. Restrições e Cuidados

1. **NÃO alterar as credenciais WiFi** — ficam versionadas por decisão do mantenedor.
2. **NÃO usar `HTTPClient`** — quebra o stream SSE.
3. **NÃO usar `WiFiClientSecure`** — o backend não tem HTTPS; TLS é o M7 do plano.
4. **NÃO mudar `AnswerCallback` para `const char*`** — quebra o zero-copy e estoura o documento JSON.
5. **NÃO remover a camada de de-framing chunked** — o payload volta a truncar de forma intermitente.
6. **NÃO reduzir o log de headers** de volta a "só a linha de status".
7. **NÃO trocar `stopProcessing()` por um `allOff()` incondicional** — apaga a resposta recém-exibida.
8. **NÃO adicionar `delay()` no caminho de renderização** — os `delay(1)` do loop/parser são o
   tick do FreeRTOS (ex-`yield()`) e **não** devem voltar a ser `yield()`.
9. O rádio do ESP32-S3 opera apenas em WiFi **2,4 GHz**.
10. `monitor_speed` no `platformio.ini` deve bater com `SERIAL_BAUD_RATE` (115200).

---

## 11. Histórico de Versões

| Versão | Mudança |
|:---:|---|
| **2.4** | Evento `status` → LED de processamento (`solving` / `idle` / `error`) |
| **2.3** | De-framing do `Transfer-Encoding: chunked`; todo header de resposta passa a ser logado |
| **2.2** | Janela de silêncio no boot: LEDs apagados após 5 min, até a 1ª resposta |
| **2.1** | Parse JSON zero-copy (`char*`) — corrige respostas longas que não acendiam os LEDs |
| **2.0** | Polling HTTPS substituído pelo stream SSE persistente; reconexão com backoff |
| **1.0** | Versão inicial por polling |

O changelog completo, com o diagnóstico de cada correção, está no topo de `src/main.cpp`.
