# ARCHITECTURE.md — CertMind ESP32-S3 Client

Documento de arquitetura interna. Para instalação, montagem e comportamento dos LEDs, veja o
**[README.md](README.md)**.

## 1. Visão Geral

Firmware **PlatformIO** para **ESP32-S3 Super Mini** que consome um stream **SSE** (Server-Sent Events)
da API CertMind e aciona os pixels de uma **barra WS2812 de 8 LEDs** (GPIO 13) conforme cada
situação emitida pelo backend. Dois canais independentes desde a v3.2: pixels **0–5 = respostas
(A–F)** e pixels **6–7 = status dedicado** (conexão e processamento).

| | |
|---|---|
| **Plataforma** | ESP32-S3 Super Mini (ESP32-S3FH4R2: 4 MB flash quad + 2 MB PSRAM quad) — framework Arduino |
| **Versão** | 3.5 |
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
│   ├── ConfigStore.h/.cpp      # Configuração persistente em NVS (defaults do Config.h)
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
│                     main.cpp (v3.3: dual-core)               │
│                                                              │
│  setup() [loopTask, core 1]:                                 │
│    leds.begin() · xQueueCreate(16, LedCommand)               │
│    xTaskCreatePinnedToCore(netTask, core 0)                  │
│                                                              │
│  netTask [core 0 — PODE bloquear]:                           │
│    wifiManager.connect() · sse.begin(handleAnswer/Status)    │
│    for(;;){ sse.loop(); saúde do stream na transição }       │
│    handleAnswer/Status: deserializeJson → LedCommand → fila  │
│                                                              │
│  loop() = uiTask [core 1, tick 5 ms — NUNCA bloqueia]:       │
│    xQueueReceive → applyLedCommand → leds.*                  │
│    leds.update() · log periódico do heap                     │
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
main.cpp: deserializeJson(g_doc, payload)                     [zero-copy]
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
                ├── "capturing"                     → leds.showCapturing()
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
| Buffers | `SSE_MAX_LINE`, `SSE_MAX_DATA`, `SSE_MAX_CHUNK` | 16384, 16384, 65535 |
| JSON | `JSON_DOC_SIZE` | 4096 bytes (sem filtro desde a v3.1/M2) |
| Barra de LED | `LED_BAR_PIN`, `LED_BAR_COUNT`, `LED_COUNT`, `LED_BRIGHTNESS`, `LED_MAX_VOLTS`/`LED_MAX_MILLIAMPS`, `LED_COLOR_A`..`LED_COLOR_F` | GPIO 13, 8, 5, 96, 5 V/600 mA, cores do D1 |
| Conexão / ocioso | `LED_CONN_BLINK_MS`, `LED_IDLE_PERIOD_MS`, `LED_IDLE_PULSE_MS` | 150 ms, 2000 ms, 80 ms |
| Janela de boot | `LED_BOOT_BLINK_MS` | 300000 ms (5 min) |
| HOLD | `LED_HOLD_TTL_MS`, `LED_HOLD_INTAKE_MS`, `LED_YESNO_BLINK_MS` | 12000 ms, 250 ms, 350 ms |
| Processando (varredura) | `LED_SOLVE_COLOR`, `LED_SOLVE_SPAN`, `LED_SOLVE_STEP_MS`, `LED_SOLVE_TRAIL`, `LED_SOLVE_FADE` | vermelho, 8 px (barra inteira), 80 ms, 3, 110 |
| Multicaptura (respiração) | `LED_CAPTURE_COLOR`, `LED_CAPTURE_PERIOD_MS`, `LED_CAPTURE_STEP_MS`, `LED_CAPTURE_MIN`, `LED_CAPTURE_MAX` | ciano, 1600 ms, 40 ms, 20, 255 |
| Pixels de status | `LED_PIX_STATUS_CONN`, `LED_PIX_STATUS_PROC`, `LED_COLOR_STATUS_*` | 6 (âmbar/violeta), 7 (ciano da multicaptura; apagado fora dela) |
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

Responsabilidade: máquina de estados da barra WS2812 — **dois canais independentes** com
animações não-bloqueantes: respostas (pixels 0–5, A–F) e status (6 = conexão, 7 = processamento).

**Canal de resposta (pixels 0–5):**

| Modo | Trigger | Comportamento | Fim |
|---|---|---|---|
| `MODE_HOLD` | `showSingle()` / `showMultiple()` / `showYesNo()` | Pixels fixos nas suas cores (yesno: "Não" piscando) | TTL de 12 s |
| `MODE_CHASE` | `showTestChase()` | Varredura A→F, 2 passadas de 120 ms/passo | Fim da animação |
| `MODE_ERROR` | `showError()` | Os 6 piscam juntos 3× (250 ms on/off) | Fim da animação |
| `MODE_SEQ` | `showSlots()` | Passo a passo com gaps, 2 passadas | Fim da animação |

**Canal de status (`renderStatus()` — roda sempre que não há solving ativo):**

| Pixel | Estado | Comportamento |
|---|---|---|
| 6 | desconectado | Âmbar piscando (150 ms) |
| 6 | conectado | Pulso violeta curto (80 ms a cada 2 s) — suprimido enquanto há resposta em exibição **ou multicaptura ativa** |
| 7 | `_capturing` | Respiração ciano (`sin8` sobre passos de `LED_CAPTURE_STEP_MS`); apagado fora da multicaptura. O solving não usa este pixel (a varredura toma a barra inteira) |

**Processando (`_processing`, `renderSolving()` — desde a v3.6):** varredura vermelha vai-e-vem
(estilo Larson scanner) na barra inteira (8 pixels), com rastro em fading (`LED_SOLVE_TRAIL`
pixels decaídos por `LED_SOLVE_FADE` a cada passo de `LED_SOLVE_STEP_MS`), **sem TTL** — até
`answer` (real ou `test`) / `error` / `idle` / `capturing` / queda do stream. Toma a barra enquanto
durar; a resposta em exibição (se o TTL não expirou) e o pixel de conexão reassumem ao terminar.

Como não há TTL, **cada um desses caminhos chama `stopSolving()` explicitamente** (`startHold()`,
`startSeq()`, `showTestChase()`, `showError()`, `showCapturing()`, `stopProcessing()`). Até a v3.7
quem encerrava a varredura era só o `idle` que fecha o solve — o que quebrou no fluxo de
multicaptura, onde o `capturing` **substitui** esse `idle`: a varredura rodava para sempre e o
`update()`, que desvia para `renderSolving()` antes de tudo, escondia o chase de `test` e o pixel 7.
Corrigido na v3.8.

**Multicaptura (`_capturing`, `showCapturing()` — desde a v3.7):** o servidor v2.9+ parqueia um
print que sozinho não resolve a questão (vários dropdowns, painel de case study) e aguarda a
próxima foto. Respiração ciano no pixel 7, **sem TTL**, até `solving` / `answer` real / `idle` /
`error` — todos encerram o modo por dentro do controller (não há comando "capture off" na fila).
Duas assimetrias deliberadas em relação ao solving: **sobrevive à queda do stream** (o print segue
parqueado no servidor; o âmbar do pixel 6 já denuncia a queda e o `status` inicial da reconexão
ressincroniza) e **não é encerrada pelo chase de `test`**, que no fluxo real precede cada
`capturing`. A respiração é quantizada em `LED_CAPTURE_STEP_MS` antes de virar fase, senão o nível
mudaria a cada tick de 5 ms da uiTask e o `flush()` transmitiria a barra a ~200 Hz.

**Prioridade de exibição:**

1. A varredura de solving, enquanto ativa, sobrepõe a barra inteira.
2. Um `answer` novo sempre interrompe a exibição atual (nos pixels de resposta).
3. `test` / erro / sequências tocam e apagam.
4. `single` / `multiple` / `yesno` ficam retidos por `LED_HOLD_TTL_MS` e apagam.
5. Blackout de boot suprime a barra inteira (respostas **e** status) até a 1ª resposta.

**Propriedades importantes:**

- **100% não-bloqueante** — toda temporização em `millis()`, zero `delay()`.
- **Blank de intake** (250 ms) — todo HOLD começa apagado, o que torna visível a chegada de
  respostas iguais consecutivas (ex.: `A` depois `A`).
- **`yesno` simultâneo** — cada afirmação acende seu pixel ao mesmo tempo: Sim fixo, Não piscando.
- **`stopProcessing()` deixou de ser condicional na v3.2** — como o processando tem pixel próprio,
  encerrar o solving nunca toca na resposta em exibição. (Até a v3.1, com status e resposta nos
  mesmos 5 LEDs, o condicional era o que impedia o `idle` pós-`answer` de apagar a resposta.)
- **Queda do stream aborta o "processando"** — `setConnected(false)` zera `_processing` porque
  ele não tem TTL; sem isso a varredura correria para sempre escondendo a perda do stream.
  **Mas não aborta a multicaptura**: o print continua parqueado no servidor, o âmbar do pixel 6
  já sinaliza a queda e o `status` inicial da reconexão ressincroniza o estado real.
- **Janela de boot** (5 min) — a barra sinaliza normalmente e depois entra em blackout total até
  a 1ª resposta; `_answerActive` implica `_firstAnswerReceived`, então o gate nunca corta uma
  resposta em exibição.
- **`flush()` centraliza o `FastLED.show()`** — os helpers só marcam `_dirty` quando algo muda;
  um frame idêntico não é retransmitido.

**Estados internos:**

```
_answerActive (bool)        → há resposta sendo exibida (pixels 0-5)
_mode (enum Mode)           → HOLD / CHASE / ERROR / SEQ
_connected (bool)           → saúde do stream (pixel 6)
_processing (bool)          → solving em andamento (varredura na barra inteira)
_capturing (bool)           → multicaptura ativa (respiração ciano no pixel 7)
_animStart (unsigned long)  → millis() do início da animação (TTL, blank, passos)
_bootMillis                 → millis() do begin(): início da janela de boot
_firstAnswerReceived        → encerra o blackout em definitivo (não rearma)
_blackoutAnnounced          → log one-shot ao entrar em blackout
_holdMask / _holdBlinkMask  → máscaras de pixels fixos / piscando no HOLD
_stepType / _stepLed / _stepCount → buffer da sequência corrente
_bar / _lastMask / _dirty   → framebuffer CRGB[8] + caches do flush()
```

### 5.5. `main.cpp`

Responsabilidade: orquestração, parsing JSON e o split dual-core (v3.3/M4).

- **`setup()`** (loopTask, core 1) — Serial, `leds.begin()`, cria a fila de `LedCommand` (16
  posições) e a `netTask` pinada no core 0.
- **`netTask()`** (core 0) — `wifiManager.connect()` (bloqueia até 15 s **só esta task**),
  `sse.begin(...)`, e o laço `sse.loop()` + saúde do stream (enfileirada **só na transição**).
  Tudo que bloqueia mora aqui: `connect()` TCP (~5 s), `Serial` dos headers.
- **`loop()` = uiTask** (core 1, tick de 5 ms) — drena a fila (`applyLedCommand()`),
  `leds.update()`, log de heap. Nunca bloqueia.
- **`handleAnswer(char* payload)`** (netTask) — decide por `hasData` + `questionType` e enfileira.
- **`handleStatus(char* payload)`** (netTask) — decide por `state` e enfileira.

**Invariantes do dual-core:**

1. A fila carrega `LedCommand` **por valor** (posições/slots/flags já decodificados) — nunca um
   ponteiro para o `_dataBuf` do `SseClient`, que é reescrito no próximo evento.
2. O `LedController` é tocado **exclusivamente** pela loopTask — é isso que dispensa mutex.
3. Fila cheia → descarta e loga (comandos são só exibição; prender a netTask seria pior).

**Parsing JSON:**

- `StaticJsonDocument<4096>` (`g_doc`) para os dados — **global**, para não pesar na pilha da
  loopTask. Sem filtro desde a v3.1 (M2): o payload inteiro é parseado e o uso do pool é logado
  a cada `answer` (`[JSON] pool=`).
- **Zero-copy** via buffer mutável (`char*`) → ativa o `StringMover` do ArduinoJson: o pool
  carrega só a estrutura, não as strings.
- Letras são normalizadas com `toupper()` e validadas em A–F; arrays são truncados em `LED_COUNT`
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

`letters` só é lida em `single`/`multiple`; `flags` em `yesno`; `slots` (posições 1–6) em
`dropdown`/`ordering`/`matching`. `explanation` e `elapsedMilliseconds` não são parseados.

### 6.5. Payload do evento `status`

```json
{ "state": "solving|capturing|idle|error", "activeSolves": 1 }
```

Sequência normal de um solve: `status solving` → `answer` → `status idle`.
Em falha: `status solving` → `status error` (**sem** `answer`).
Ao abrir a conexão o servidor manda um `status` com o estado atual, então reconectar no meio de um
processamento já traz `solving` — é assim que o estado ressincroniza.

**Multicaptura (`capturing`, servidor v2.9+).** Quando uma foto sozinha não basta para responder
(questão com vários dropdowns — só uma lista abre por foto — ou painel de case study), o servidor
parqueia o print e aguarda a próxima foto da mesma questão:

```text
event: answer
data: {"hasData":false,"questionType":"test","letters":[],"answerText":"-"}

event: status
data: {"state":"capturing","activeSolves":0}
```

O `capturing` chega logo após o `answer` de `test` e **substitui o `idle` final**, virando o
estado-base até chegar `solving`, um `answer` real, `idle` (lote limpo) ou `error`. O `status`
inicial da (re)conexão também pode vir `capturing` — é assim que o aviso sobrevive a uma
reconexão. `: ping` não muda estado; `state` desconhecido continua sendo tratado como `idle`.

---

## 7. Mapeamento de Pinos

Um único pino de dados: **GPIO 13 → DIN da barra WS2812 de 8 pixels** (FastLED sobre RMT,
ordem de cor GRB). O mapeamento lógico fica nos pixels:

| Pixel | Cor (`LED_COLOR_*`) | Função |
|---|---|---|
| 0 | Verde    | Resposta A (posição 1) |
| 1 | Amarelo  | Resposta B (posição 2) |
| 2 | Vermelho | Resposta C (posição 3) |
| 3 | Azul     | Resposta D (posição 4) |
| 4 | Branco   | Resposta E (posição 5) |
| 5 | Magenta  | Resposta F (posição 6) |
| 6 | Âmbar / Violeta | Status: conexão (âmbar piscando = (re)conectando; pulso violeta = ocioso, suprimido com resposta em exibição ou multicaptura ativa) |
| 7 | Ciano | Status: multicaptura (`capturing`) — respiração ciano; apagado fora dela. O solving não usa este pixel (a varredura vermelha toma a barra inteira) |

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
| Blink de conexão | 150 ms | Pixel 6 em âmbar ao conectar/reconectar |
| Pulso ocioso | 80 ms a cada 2 s | Heartbeat violeta no pixel 6 |
| Janela de boot | 5 min | Barra sinaliza antes do blackout total |
| Processando | 80 ms/pixel | Varredura vermelha vai-e-vem na barra inteira com rastro, sem TTL |
| Chase (passo) | 120 ms | 2 passadas nos 6 pixels de resposta |
| Erro | 250 ms on/off × 3 | Os 6 pixels de resposta juntos |
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
| **Zero-copy JSON** | Buffer mutável (`char*`) ativa o `StringMover` do ArduinoJson — o pool carrega só a estrutura, não as strings |
| **Sem filtro JSON (v3.1/M2)** | O filtro só existia para ignorar `explanation` e caber nos 512 bytes do 8266; com pool de 4096 o payload inteiro é parseado e o uso é logado (`[JSON] pool=`) |
| **Erro visível no `answer`** | `showError()` em vez de `return` silencioso — o silêncio era o que escondia bugs (v2.1) |
| **Erro *invisível* no `status`** | Piscar os pixels de resposta num status corrompido seria lido como "questão ilegível"; fica só no log |
| **Status em pixels dedicados (v3.2)** | Conexão/processamento não disputam mais pixels com as respostas; o `solving` deixa de apagar a resposta exibida e o `stopProcessing()` condicional (cicatriz da v2.4) se dissolveu |
| **Processando sem TTL, abortado na queda** | O andamento pode demorar; mas sem TTL, uma queda de stream deixaria o LED piscando para sempre |
| **Multicaptura sobrevive à queda (v3.7)** | Assimetria deliberada com o solving: o print segue parqueado no servidor, então o aviso precisa atravessar a reconexão. A queda já é visível no pixel 6 (âmbar) e o `status` inicial do stream reaberto ressincroniza |
| **Multicaptura encerrada por dentro do controller (v3.7)** | Sem `LED_CMD_CAPTURE_OFF`: `showProcessing()`/`startHold()`/`startSeq()`/`showError()`/`stopProcessing()` limpam o modo. Um comando separado abriria espaço para ordem invertida na fila e estados divergentes |
| **Solving encerrado por `stopSolving()` em todo caminho (v3.8)** | A varredura não tem TTL, e até a v3.7 quem a encerrava era só o `idle` que fecha o solve. Como o `capturing` **substitui** esse `idle`, a varredura ficava eterna e escondia o pixel 7. A relação virou mão dupla: `showProcessing()` limpa a multicaptura e todo evento que fecha um solve limpa a varredura |
| **Respiração quantizada em 40 ms (v3.7)** | Sem quantizar, o nível mudaria a cada tick de 5 ms da uiTask e o `flush()` transmitiria a barra a ~200 Hz, contra o contrato de só transmitir quando o frame muda |
| **Blackout pós-boot** | Evita poluição luminosa em uso prolongado sem eventos; encerra em definitivo na 1ª resposta |
| **Chunk-size sem dígito hex apenas rearma** | Tratá-lo como tamanho 0 dispararia um retry extra e pularia um degrau do backoff |
| **Dual-core com fila por valor (v3.3)** | O que bloqueia (connect TCP ~5 s, WiFi 15 s, Serial) mora na netTask/core 0; a animação roda na loopTask/core 1 e nunca engasga. Comandos por valor + LedController exclusivo da loopTask = zero mutex |
| **Todo `millis()`** | Responsividade máxima; a uiTask roda num tick de 5 ms |
| **Backoff progressivo** | Evita martelar o backend com reconexões imediatas |
| **Replay pós-reconexão (v3.4)** | `id:` rastreado → `Last-Event-ID` no GET seguinte (eventos da janela de reconexão deixam de ser perdidos, se o servidor suportar); `retry:` do servidor honrado no 1º degrau do backoff, com faixa sã de 250 ms–60 s |
| **Config NVS só-no-flash (v3.5)** | `ConfigStore::set()` escreve só no NVS, nunca na cópia em RAM (efeito após restart) — é o que dispensa lock entre a CLI (loopTask) e a netTask, que lê os getters |
| **OTA sinaliza via fila (v3.5)** | Os callbacks do ArduinoOTA rodam na netTask; o pixel de processamento pisca durante o update por LedCommand, preservando o invariante "LedController só na loopTask" |

---

## 10. Restrições e Cuidados

1. **NÃO alterar as credenciais WiFi** — ficam versionadas por decisão do mantenedor.
2. **NÃO usar `HTTPClient`** — quebra o stream SSE.
3. **NÃO usar `WiFiClientSecure`** — o backend não tem HTTPS; TLS é o M7 do plano.
4. **NÃO mudar `AnswerCallback` para `const char*`** — quebra o zero-copy e estoura o documento JSON.
5. **NÃO remover a camada de de-framing chunked** — o payload volta a truncar de forma intermitente.
6. **NÃO reduzir o log de headers** de volta a "só a linha de status".
7. **NÃO voltar o "processando" para os pixels de resposta** — o canal separado é o que permite ao
   `solving`/`idle` nunca interferir na resposta em exibição.
7b. **NÃO abortar a multicaptura em `setConnected(false)`** nem encerrá-la em `showTestChase()` —
   as duas assimetrias em relação ao solving são deliberadas (ver seção 9).
7c. **NÃO tirar o `stopSolving()` de nenhum caminho que fecha um solve** — a varredura não tem TTL
   e contar só com o `idle` final foi exatamente o bug da v3.7 (ver seção 9).
8. **NÃO adicionar `delay()` no caminho de renderização** — os `delay()` de tick (5 ms na uiTask,
   1 ms na netTask/parser) são o tick do FreeRTOS (ex-`yield()`) e **não** devem voltar a ser `yield()`.
9. O rádio do ESP32-S3 opera apenas em WiFi **2,4 GHz**.
10. **NÃO enfileirar ponteiros na fila de LED** (só `LedCommand` por valor) e **NÃO chamar `leds.*`
    fora da loopTask** — são os dois invariantes que sustentam o dual-core sem mutex.
11. `monitor_speed` no `platformio.ini` deve bater com `SERIAL_BAUD_RATE` (115200).

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
