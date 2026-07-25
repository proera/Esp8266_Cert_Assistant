# ARCHITECTURE.md — CertMind ESP8266 Client

## 1. Visão Geral

Firmware **PlatformIO** para **ESP8266 (D1 Mini)** que consome um stream **SSE** (Server-Sent Events) da API CertMind e aciona **5 LEDs** conforme cada situação emitida pelo backend.

- **Plataforma:** ESP8266 (D1 Mini) — Arduino framework
- **Versão:** 2.2
- **Environment:** `[env:d1_mini]`
- **Dependências:** ArduinoJson ^6.21.5 (única lib externa)
- **Protocolo:** HTTP/1.1 GET → SSE (texto claro, sem TLS)

---

## 2. Estrutura do Projeto

```
Esp8266_Cert_Assistant/
├── src/
│   ├── Config.h              # Constantes centralizadas (#define)
│   ├── main.cpp              # Orquestrador: setup(), loop(), handleAnswer()
│   ├── WiFiManager.h/.cpp    # Conexão WiFi (STA mode)
│   ├── SseClient.h/.cpp      # Cliente SSE: TCP, parser, reconexão
│   └── LedController.h/.cpp  # Máquina de estados dos LEDs (não-bloqueante)
├── platformio.ini            # Configuração PlatformIO
├── plans/
│   └── plano_detalhado.md    # Plano original v1.0 (polling)
├── CLAUDE.md                 # Diretrizes para assistentes IA
├── README.md                 # Documentação do projeto
└── .vscode/                  # Configurações do editor
```

---

## 3. Diagrama de Componentes

```
┌─────────────────────────────────────────────────────────┐
│                      main.cpp                           │
│                                                         │
│  setup():                                               │
│    leds.begin()                                         │
│    wifiManager.connect()                                │
│    sse.begin(handleAnswer)                              │
│                                                         │
│  loop():                                                │
│    leds.update()                                        │
│    sse.loop()                                           │
│    leds.setConnected(sse.isStreaming())                 │
│                                                         │
│  handleAnswer(char* payload):                           │
│    deserializeJson → decide padrão LED                  │
└────────────┬──────────────────┬─────────────────────────┘
             │                  │
    ┌────────▼────────┐  ┌─────▼──────────────┐
    │   WiFiManager   │  │    LedController    │
    │                 │  │                     │
    │ • connect()     │  │ • showSingle()      │
    │ • isConnected() │  │ • showMultiple()    │
    │ • getIP()       │  │ • showYesNo()       │
    │ • reconnect()   │  │ • showSlots()       │
    └─────────────────┘  │ • showTestChase()   │
                         │ • showError()       │
                         │ • update()          │
                         │ • setConnected()    │
                         └─────────────────────┘
             │
    ┌────────▼────────┐
    │    SseClient     │
    │                  │
    │ • begin(cb)      │
    │ • loop()         │
    │ • isStreaming()  │
    │                  │
    │ Estados:         │
    │  ST_DISCONNECTED │
    │  ST_HEADERS      │
    │  ST_STREAMING    │
    └──────────────────┘
```

---

## 4. Fluxo de Dados

```
CertMind Backend
    │
    │  TCP GET /api/exam/stream HTTP/1.1
    │  Connection: keep-alive
    │  Accept: text/event-stream
    │
    ▼
SseClient::tryConnect()
    │
    │  Valida headers (200 + text/event-stream)
    │  state = ST_STREAMING
    │
    ▼
SseClient::pump()  [byte-a-byte, não-bloqueante]
    │
    │  Parse SSE: event:, data:, :comment (keepalive)
    │
    ▼
SseClient::dispatchEvent()
    │
    │  Se event == "answer" → chama handleAnswer(char*)
    │
    ▼
main.cpp: handleAnswer()
    │
    │  ArduinoJson::deserializeJson (com filtro, ignora explanation)
    │
    │  Extrai: hasData, questionType, letters, flags, slots, slotCount
    │
    ▼
Decisão de Padrão LED:
    │
    ├── questionType == "test"         → leds.showTestChase()
    ├── hasData == false               → leds.showError()
    ├── "single"                       → leds.showSingle()
    ├── "multiple"                     → leds.showMultiple()
    ├── "yesno"                        → leds.showYesNo()
    ├── "dropdown"/"ordering"/"matching"→ leds.showSlots()
    └── desconhecido                   → leds.showError()
    │
    ▼
LedController::update()
    │
    ├── Resposta ativa → renderiza modo atual (HOLD/CHASE/ERROR/SEQ)
    └── Sem resposta  → renderiza conexão (A+E blink) ou ocioso (A pulse)
```

---

## 5. Detalhamento dos Módulos

### 5.1. `Config.h`

Centraliza **todas** as constantes via `#define`. Nenhuma configuração espalhada pelo código.

| Categoria | Constantes | Valores Exemplo |
|-----------|-----------|-----------------|
| WiFi | `WIFI_SSID`, `WIFI_PASSWORD`, `WIFI_MAX_RETRY_ATTEMPTS`, `WIFI_RETRY_DELAY_MS` | `"Sagaz"`, `"Amarelo%4815"`, 30, 500ms |
| Stream | `STREAM_HOST`, `STREAM_PORT`, `STREAM_PATH`, `STREAM_TIMEOUT_MS` | `192.168.15.38`, 8090, `/api/exam/stream`, 40000ms |
| Backoff | `STREAM_BACKOFF_TABLE` | `{1000, 2000, 5000, 10000, 20000, 30000}` ms |
| Buffers | `SSE_MAX_LINE`, `SSE_MAX_DATA`, `JSON_DOC_SIZE`, `JSON_FILTER_SIZE` | 4096, 4096, 512, 252 bytes |
| Pinos | `LED_PIN_A`..`LED_PIN_E` | D3, D2, D5, D1, D7 |
| LED Timings | `LED_CONN_BLINK_MS`, `LED_HOLD_TTL_MS`, `LED_BOOT_BLINK_MS`, etc. | 150ms, 12000ms, 300000ms |
| Serial | `SERIAL_BAUD_RATE` | 115200 |

### 5.2. `WiFiManager`

Responsabilidade: conexão WiFi no modo STA (Station).

- **`connect()`**: Conecta ao WiFi com até 30 tentativas. **Bloqueante** — espera resultado antes de prosseguir. Usado apenas na inicialização.
- **`isConnected()`**: Verifica status da conexão.
- **`getIP()`**: Retorna o IP atribuído.
- **`reconnect()`**: Tenta reconexão (usado internamente pelo `SseClient`).

> **Nota:** Credenciais WiFi ficam em texto plano no código por decisão do mantenedor (versionadas no repositório).

### 5.3. `SseClient`

Responsabilidade: gerenciar a conexão SSE ponta-a-ponta.

**Estados da Máquina:**
```
ST_DISCONNECTED → ST_HEADERS → ST_STREAMING
                         ↑          │
                         └──────────┘ (timeout/falha)
```

**Componentes internos:**
- `tryConnect()`: Abre TCP, envia GET manual (sem `HTTPClient`)
- `pump()`: Lê byte-a-byte, monta linhas, não-bloqueante
- `processHeaderLine()`: Valida `HTTP/1.1 200` e `Content-Type: text/event-stream`
- `processSseLine()`: Parse do protocolo SSE (`event:`, `data:`, `:` comments)
- `dispatchEvent()`: Ao encontrar linha em branco, dispara o callback se o evento era `answer`
- `scheduleRetry()`: Backoff exponencial com tabela definida em `Config.h`
- `checkTimeout()`: 40s sem dados → reconecta

**Dados internos:**
- `_dataBuf` (4096 bytes): Buffer mutável para payloads SSE
- `_lineBuf` (4096 bytes): Buffer de linha atual
- `_evtBuf` (256 bytes): Nome do evento SSE atual

### 5.4. `LedController`

Responsabilidade: máquina de estados dos 5 LEDs com animações não-bloqueantes.

**Modos de Exibição:**

| Modo | Trigger | Comportamento |
|------|---------|---------------|
| **Conectando** | `setConnected(false)` | LEDs A+E piscam (150ms) |
| **Ocioso** | `setConnected(true)` | LED A pulsos curtos (80ms, a cada 2s) |
| **HOLD** | `showSingle()`/`showMultiple()`/`showYesNo()` | LEDs fixos por 12s, depois volta ao ocioso |
| **CHASE** | `showTestChase()` | Corrida de LEDs (2 passagens, 120ms/step) |
| **ERROR** | `showError()` | Todos piscam 3x (250ms on/off) |
| **SEQ** | `showSlots()` | Sequência LED-a-LED com gaps e piscadas |

**Propriedades importantes:**
- **100% não-bloqueante**: Toda temporização usa `millis()`, zero `delay()`
- **Resposta interrompe**: Novo `answer` sempre sobrepõe o estado atual
- **Blank de intake** (250ms): LEDs apagados no início de cada HOLD para tornar visível respostas consecutivas iguais
- **Janela de boot** (5 min): LEDs sinalizam normalmente, depois entram em blackout até a 1ª resposta
- **`yesno` simultâneo**: Sim = fixo, Não = piscando; cada afirmação acende seu LED

**Estados internos:**
```
_answerActive (bool)    → Resposta sendo exibida
_answerMode (enum)      → HOLD / CHASE / ERROR / SEQ
_firstAnswerReceived    → Encerra blackout definitivamente
_animStart              → millis() do início da animação
_holdMask / _holdBlinkMask → Máscaras de quais LEDs acender
```

### 5.5. `main.cpp`

Responsabilidade: orquestração e parsing JSON.

- **`setup()`**: Inicializa Serial, LEDs, WiFi, e SSE
- **`loop()`**: Chama `leds.update()`, `sse.loop()`, atualiza status de conexão, loga heap
- **`handleAnswer(char* payload)`**: O ponto central de decisão

**Parsing JSON:**
- `StaticJsonDocument<512>` para dados
- `StaticJsonDocument<252>` para filtro (ignora `explanation`)
- Zero-copy via buffer mutável (`char*`) → ArduinoJson `StringMover`
- Extração: `hasData`, `questionType`, `letters[]`, `flags[]`, `slots[]`, `slotCount`

---

## 6. Protocolo de Comunicação

### 6.1. Transporte

| Camada | Detalhes |
|--------|----------|
| Rede | TCP/IP via `WiFiClient` (sockets raw) |
| Segurança | **Nenhuma** — HTTP puro, sem TLS/SSL |
| Aplicação | HTTP/1.1 GET, `Connection: keep-alive` |

### 6.2. Request HTTP

```http
GET /api/exam/stream HTTP/1.1
Host: 192.168.15.38:8090
Connection: keep-alive
Accept: text/event-stream
```

### 6.3. SSE (Server-Sent Events)

Formato padrão WHATWG:
```
: ping                          ← comentário (keepalive)

event: answer                   ← tipo do evento
data: {"hasData":true,...}      ← payload JSON

                                ← linha em branco = fim do evento
```

### 6.4. Payload JSON (SolverOutput)

```json
{
  "hasData": true,
  "questionType": "single|multiple|yesno|test|dropdown|ordering|matching",
  "letters": ["A", "C"],
  "flags": [true, false, true, false, false],
  "slots": [{"letter":"A","correct":true}],
  "slotCount": 3,
  "answerText": "..."
}
```

---

## 7. Mapeamento de Pinos

| Pino GPIO | Pino D1 Mini | Cor | Função |
|-----------|-------------|-----|--------|
| GPIO0 | D3 | Verde | LED A (posição 1) |
| GPIO4 | D2 | Amarelo | LED B (posição 2) |
| GPIO14 | D5 | Vermelho | LED C (posição 3) |
| GPIO5 | D1 | Azul | LED D (posição 4) |
| GPIO13 | D7 | Branco | LED E (posição 5) |

---

## 8. Temporização e Non-Blocking

Todo o firmware opera sem `delay()`. Principais temporizações:

| Evento | Duração | Descrição |
|--------|---------|-----------|
| Blink de conexão | 150ms | A+E piscam ao reconectar |
| Pulso ocioso | 80ms a cada 2s | A pisca brevemente |
| Boot blink | 5 min (300s) | LEDs sinalizam antes do blackout |
| Chase passo | 120ms | 2 passagens nos 5 LEDs |
| Error piscar | 250ms on/off × 3 | Todos os LEDs |
| Seq passo | 1500ms | LED-a-LED nas sequências |
| Hold TTL | 12s | Resposta fixa antes de voltar ao ocioso |
| Hold intake | 250ms | Blank no início de cada resposta |
| Yes/No blink | 350ms | LED de "Não" pisca |

---

## 9. Decisões de Design Críticas

| Decisão | Motivação |
|---------|-----------|
| **Sem `HTTPClient`** | Bloqueia esperando corpo terminar — incompatível com stream SSE infinito |
| **Sem `WiFiClientSecure`** | ESP8266 com TLS consome muita RAM; backend é local |
| **Zero-copy JSON** | Buffer mutável (`char*`) ativa `StringMover` do ArduinoJson (~160/512 bytes vs ~507/512 com cópia) |
| **Filtro JSON** | Ignora campo `explanation` (mais longo) para caber em 512 bytes |
| **Erro visível** | `showError()` em vez de `return` silencioso — evita bugs escondidos (v2.1 fix) |
| **Blackout pós-boot** | Evita poluição luminosa; LEDs ficam apagados até a 1ª resposta |
| **Todo `millis()`** | Responsividade máxima; `loop()` roda a milhares de Hz |
| **Backoff exponencial** | Evita sobrecarregar backend com reconexões imediatas |

---

## 10. Restrições e Cuidados

1. **NÃO alterar credenciais WiFi** — ficam versionadas por decisão do mantenedor
2. **NÃO usar `HTTPClient`** — quebra o stream SSE
3. **NÃO usar `WiFiClientSecure`** — consome RAM demais
4. **NÃO mudar `AnswerCallback` para `const char*`** — quebra zero-copy e estoura JSON
5. **NÃO adicionar `delay()`** — quebra não-bloqueante
6. ESP8266 opera apenas em WiFi **2.4 GHz**
7. `monitor_speed` no `platformio.ini` deve bater com `SERIAL_BAUD_RATE` (115200)
