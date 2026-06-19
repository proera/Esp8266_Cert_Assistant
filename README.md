# CertMind — Cliente de Stream ESP8266 (D1 Mini)

Firmware para D1 Mini (ESP8266) que consome o stream da API **CertMind** por **uma única conexão HTTP persistente (SSE — Server-Sent Events)** e aciona **5 LEDs (A–E)** conforme cada situação emitida pelo backend.

O ESP8266 é **apenas consumidor**: abre um `GET` e fica escutando. Não faz `POST`, não envia imagem, não faz request/response.

> **Versão 2.0** — substitui o transporte por polling HTTPS (v1.0) por stream SSE persistente em texto claro. Veja o changelog no topo de `src/main.cpp`.

## 🏗️ Arquitetura

Projeto **PlatformIO** com arquitetura modular (separação de responsabilidades):

| Módulo | Responsabilidade |
|---|---|
| `src/Config.h` | Configurações centralizadas: WiFi, endpoint do stream, pinos, timings dos padrões de LED e backoff de reconexão. |
| `src/WiFiManager.{h,cpp}` | Conexão WiFi inicial (modo STA) e helpers de status. |
| `src/SseClient.{h,cpp}` | Conexão GET persistente, validação de headers, parser SSE não-bloqueante e reconexão automática com backoff. |
| `src/LedController.{h,cpp}` | Máquina de estados dos 5 LEDs (toda em `millis()`, sem `delay()`). |
| `src/main.cpp` | Liga os módulos, faz o parse do JSON e decide o comportamento dos LEDs. |

Fluxo: `SseClient` recebe um evento `answer` → `main.cpp` parseia o JSON → decide por `hasData` + `questionType` → `LedController` exibe o padrão correspondente.

## 📋 Requisitos

### Hardware
- **1× D1 Mini (ESP8266)** ou clone Wemos
- **5× LEDs** (Verde, Amarelo, Vermelho, Azul, Branco) — posições/letras A–E
- **5× Resistores 220 Ω** (um por LED)
- Protoboard, jumpers e cabo USB Micro-B

### Software
- **[PlatformIO](https://platformio.org/)** (CLI ou extensão do VS Code)
- A plataforma `espressif8266` e a biblioteca **ArduinoJson 6.x** são instaladas automaticamente a partir do `platformio.ini`.

## 🔌 Esquema de Conexão

| LED | Cor | Pino D1 Mini | GPIO | Posição/Letra | Resistor |
|-----|-----|--------------|------|---------------|----------|
| A | Verde | D3 | GPIO0 | 1 | 220 Ω |
| B | Amarelo | D2 | GPIO4 | 2 | 220 Ω |
| C | Vermelho | D5 | GPIO14 | 3 | 220 Ω |
| D | Azul | D1 | GPIO5 | 4 | 220 Ω |
| E | Branco | D7 | GPIO13 | 5 | 220 Ω |

```
D1 Mini          Resistor      LED
--------         --------      -----
D3 (GPIO0)  -->  220Ω -->  (+) LED Verde   (-) --> GND
D2 (GPIO4)  -->  220Ω -->  (+) LED Amarelo (-) --> GND
D5 (GPIO14) -->  220Ω -->  (+) LED Vermelho(-) --> GND
D1 (GPIO5)  -->  220Ω -->  (+) LED Azul    (-) --> GND
D7 (GPIO13) -->  220Ω -->  (+) LED Branco  (-) --> GND
```

Conecte o terminal negativo (−) de cada LED ao GND do D1 Mini.

## ⚙️ Configuração

Toda a parametrização fica em **`src/Config.h`**.

### Endpoint do stream

```cpp
#define STREAM_HOST "192.168.15.38"
#define STREAM_PORT 8090
#define STREAM_PATH "/api/exam/stream"
```

> **HTTP puro, sem TLS/HTTPS.** Em dev local, basta trocar host/porta (ex.: `192.168.15.38:5267`) — a lógica é a mesma.

### Credenciais WiFi

```cpp
#define WIFI_SSID "..."
#define WIFI_PASSWORD "..."
```

> O ESP8266 só opera em redes **2,4 GHz**.

### Outros ajustes disponíveis
- **Pinos dos LEDs:** `LED_PIN_A` … `LED_PIN_E`
- **Timeout / reconexão:** `STREAM_TIMEOUT_MS`, `STREAM_BACKOFF_TABLE`
- **Timings dos padrões de LED:** `LED_CONN_BLINK_MS`, `LED_IDLE_*`, `LED_CHASE_*`, `LED_ERROR_*`, `LED_SEQ_*`

## 📤 Build, Upload e Monitor

Toolchain é **PlatformIO** (ambiente único `[env:d1_mini]`):

```bash
pio run                 # Compila o firmware
pio run --target upload # Compila e grava no D1 Mini (USB)
pio device monitor      # Monitor serial (115200 baud)
pio run --target clean  # Limpa artefatos de build
```

## 📡 Como o stream funciona

- **Método:** `GET {STREAM_HOST}:{STREAM_PORT}{STREAM_PATH}` — `Content-Type: text/event-stream`.
- A conexão é **aberta uma vez e fica viva indefinidamente**; o servidor empurra eventos conforme ocorrem.
- Linhas iniciadas por `:` são comentários de prova de vida (`: connected` na abertura, `: ping` a cada 15 s).
- Um evento `answer` traz um JSON (`SolverOutput`) com os campos abaixo. O firmware lê apenas o necessário (ignora `explanation` no parse, via filtro do ArduinoJson):

| Campo | Tipo | Significado |
|---|---|---|
| `hasData` | bool | `true` se a questão foi lida; `false` se ilegível ou modo Test |
| `questionType` | string | `single`, `multiple`, `yesno`, `dropdown`, `ordering`, `matching` ou `test` |
| `letters` | string[] | Letras A–E (`single`/`multiple`) |
| `flags` | bool[] | Sim/Não por afirmação (`yesno`) |
| `slots` | int[] | Posições 1–5 por slot (`dropdown`/`ordering`/`matching`) |
| `slotCount` | int | Nº de afirmações/lacunas/itens |
| `answerText` | string | Resposta legível |
| `explanation` | string | Justificativa (não usada pelo firmware) |
| `elapsedMilliseconds` | long | Tempo de processamento no servidor |

## 💡 Comportamento dos LEDs

Não há LED de status dedicado — a saúde da conexão e as respostas compartilham os 5 LEDs com padrões distintos. **Resposta tem prioridade sobre conexão**, e um `answer` novo sempre interrompe a exibição atual.

### Estados de conexão (somente quando não há resposta ativa)

| Situação | Padrão |
|---|---|
| Conectando / sem WiFi / reconectando | LEDs das pontas (**A e E**) piscam juntos rápido (~150 ms) |
| Conectado, ocioso | Heartbeat discreto: **LED A** dá 1 pulso curto (~80 ms) a cada ~2 s |
| Segurando resposta `single`/`multiple` | Mantém a(s) posição(ões) acesa(s), sem heartbeat |

### Eventos `answer`

| Situação | Padrão |
|---|---|
| `questionType == "test"` | Varredura (chase) A→B→C→D→E, 2×, e volta ao ocioso |
| `hasData == false` (ilegível) | 5 LEDs piscam juntos 3× (~250 ms on/off) e apagam |
| Erro ao parsear o JSON do evento | Mesmo padrão de erro (5 LEDs piscando 3×) — em vez de falhar em silêncio |
| `single` | 1 LED aceso (a letra), **mantido** até o próximo `answer` |
| `multiple` | LEDs das letras acesos simultaneamente, **mantidos** |
| `yesno` | **Sequencial** por afirmação: Sim = aceso fixo, Não = piscando rápido (~1,5 s cada, gap ~0,4 s); 2 passadas |
| `dropdown` / `ordering` / `matching` | **Sequencial** acendendo a posição (1–5) de cada slot, na ordem; 2 passadas |

Sequências com mais de 5 itens são truncadas para 5 (com aviso no Serial). Slot fora de 1–5 → pisca os 5 juntos 1× naquele passo e segue.

## 🔁 Reconexão automática

Reconecta se (a) o socket cair, (b) o WiFi cair, ou (c) passar `STREAM_TIMEOUT_MS` (~40 s) sem nenhuma linha. Backoff progressivo **1 → 2 → 5 → 10 → 20 → 30 s (máx)**, zerado quando o stream reabre. Durante a reconexão, os LEDs mostram o padrão "pontas A+E piscando".

## 🧪 Testes / critérios de aceite

> Os `POST` abaixo são disparados **de um PC**, apenas para gerar eventos no stream — nada disso roda no ESP.

1. **Conexão viva:** Serial mostra `[SSE] Stream aberto`; `: ping` a cada 15 s sem reconectar; LED A em heartbeat quando ocioso.
2. **Evento de teste (sem custo de IA):** `POST {BASE}/api/exam/solve` com `Test=true` (multipart) → chase A→E em < ~1 s.
3. **single / multiple:** confirme 1 LED / vários LEDs acesos e mantidos.
4. **yesno:** confirme a sequência Sim=fixo / Não=piscando, na ordem das afirmações.
5. **dropdown / ordering / matching:** confirme a sequência acendendo a posição de cada slot.
6. **Ilegível:** force `hasData=false` → 5 LEDs piscando juntos 3×.
7. **Reconexão:** derrube WiFi/servidor → padrão A+E piscando + log de backoff; ao voltar, reconecta e o backoff zera.
8. **Heap:** acompanhe `[HEAP] livre=` (a cada 10 s) — deve permanecer estável após muitos eventos/reconexões.

## 🛠️ Solução de problemas

- **WiFi não conecta:** confira SSID/senha e use rede 2,4 GHz (o ESP8266 não suporta 5 GHz).
- **`[SSE] status HTTP != 200` ou reconexão constante:** verifique se `STREAM_HOST`/`STREAM_PORT`/`STREAM_PATH` apontam para o endpoint correto e se o servidor está acessível na rede.
- **Erro de build (`ESP8266WiFi.h`/`ArduinoJson.h`):** rode `pio run` — o PlatformIO baixa a plataforma e as dependências automaticamente.

## 📚 Recursos

- [Documentação ESP8266 Arduino core](https://arduino-esp8266.readthedocs.io/)
- [Pinout D1 Mini](https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/)
- [ArduinoJson](https://arduinojson.org/)
- [Especificação SSE (WHATWG)](https://html.spec.whatwg.org/multipage/server-sent-events.html)

## 👤 Autor

Proera — 2026
