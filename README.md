<div align="center">

# CertMind · ESP8266 Stream Client

**Firmware para D1 Mini que traduz o stream SSE da API CertMind em cinco LEDs.**

Uma única conexão HTTP persistente. Zero `delay()`. Zero polling.

<br>

![Firmware](https://img.shields.io/badge/firmware-v2.4-2ea44f?style=flat-square)
![Board](https://img.shields.io/badge/board-D1_Mini_(ESP8266)-E7352C?style=flat-square&logo=espressif&logoColor=white)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D?style=flat-square&logo=arduino&logoColor=white)
![Build](https://img.shields.io/badge/build-PlatformIO-FF7F00?style=flat-square&logo=platformio&logoColor=white)
![ArduinoJson](https://img.shields.io/badge/ArduinoJson-6.21.5-5E97D0?style=flat-square)
![Transporte](https://img.shields.io/badge/transporte-SSE_sobre_HTTP-757575?style=flat-square)

</div>

---

O ESP8266 é **apenas consumidor**: abre um `GET`, mantém a conexão viva e escuta.
Não faz `POST`, não envia imagem, não faz request/response.

O backend difunde dois eventos — `answer` (a resposta resolvida) e `status` (andamento do
processamento) — e o firmware transforma cada um em um padrão de LED inconfundível.

```bash
pio run --target upload && pio device monitor
```

### Sumário

[Arquitetura](#arquitetura) ·
[Requisitos](#requisitos) ·
[Montagem](#montagem) ·
[Configuração](#configuração) ·
[Build](#build-upload-e-monitor) ·
[O stream](#o-stream) ·
[A linguagem dos LEDs](#a-linguagem-dos-leds) ·
[Reconexão](#reconexão-automática) ·
[Testes](#testes--critérios-de-aceite) ·
[Problemas](#solução-de-problemas)

---

## Arquitetura

Projeto **PlatformIO** com ambiente único `[env:d1_mini]` e uma responsabilidade por arquivo.

```mermaid
flowchart LR
    API["<b>Backend CertMind</b><br/>GET /api/exam/stream<br/>text/event-stream"]
    SSE["<b>SseClient</b><br/>de-framing chunked<br/>parser SSE não-bloqueante<br/>reconexão com backoff"]
    MAIN["<b>main.cpp</b><br/>handleAnswer()<br/>handleStatus()"]
    LED["<b>LedController</b><br/>máquina de estados<br/>100% millis()"]
    OUT(["LEDs A–E"])

    API -->|"event: answer<br/>event: status"| SSE
    SSE --> MAIN
    MAIN --> LED
    LED --> OUT
```

| Módulo | Responsabilidade |
|---|---|
| `src/Config.h` | Toda a parametrização via `#define`: WiFi, endpoint, pinos, timings dos padrões, backoff, tetos de buffer. |
| `src/WiFiManager.{h,cpp}` | Conexão WiFi inicial (modo STA) e helpers de status. |
| `src/SseClient.{h,cpp}` | Abre o `GET`, valida headers, desmonta o `Transfer-Encoding: chunked`, faz o parser SSE linha-a-linha sem bloquear e reconecta com backoff. |
| `src/LedController.{h,cpp}` | Máquina de estados dos 5 LEDs — todo o tempo medido em `millis()`, nenhum `delay()`. |
| `src/main.cpp` | Liga os módulos, parseia o JSON (com filtro do ArduinoJson) e decide o padrão. |

> Detalhamento interno (diagramas de estado, orçamento de memória, decisões de projeto):
> **[ARCHITECTURE.md](ARCHITECTURE.md)**. Diretrizes para assistentes de IA: **[CLAUDE.md](CLAUDE.md)**.

---

## Requisitos

<table>
<tr><th align="left">Hardware</th><th align="left">Software</th></tr>
<tr valign="top"><td>

- 1× **D1 Mini (ESP8266)** ou clone Wemos
- 5× **LEDs** — verde, amarelo, vermelho, azul, branco
- 5× **Resistores 220 Ω** (um por LED)
- Protoboard, jumpers e cabo USB Micro-B

</td><td>

- **[PlatformIO](https://platformio.org/)** — CLI ou extensão do VS Code
- A plataforma `espressif8266` e o **ArduinoJson 6.x** são baixados automaticamente a partir do `platformio.ini`

</td></tr>
</table>

> [!IMPORTANT]
> O ESP8266 só opera em redes **2,4 GHz**.

---

## Montagem

| LED | Cor | Pino | GPIO | Posição / Letra | Resistor |
|:---:|-----|:----:|:----:|:---------------:|:--------:|
| **A** | 🟢 Verde    | `D3` | GPIO0  | 1 | 220 Ω |
| **B** | 🟡 Amarelo  | `D2` | GPIO4  | 2 | 220 Ω |
| **C** | 🔴 Vermelho | `D5` | GPIO14 | 3 | 220 Ω |
| **D** | 🔵 Azul     | `D1` | GPIO5  | 4 | 220 Ω |
| **E** | ⚪ Branco   | `D7` | GPIO13 | 5 | 220 Ω |

```
   D1 Mini              Resistor            LED              Comum
   ─────────            ────────            ─────            ─────
   D3  (GPIO0)  ──────▶  220 Ω  ──────▶  (+) Verde    (−) ──┐
   D2  (GPIO4)  ──────▶  220 Ω  ──────▶  (+) Amarelo  (−) ──┤
   D5  (GPIO14) ──────▶  220 Ω  ──────▶  (+) Vermelho (−) ──┼──▶ GND
   D1  (GPIO5)  ──────▶  220 Ω  ──────▶  (+) Azul     (−) ──┤
   D7  (GPIO13) ──────▶  220 Ω  ──────▶  (+) Branco   (−) ──┘
```

---

## Configuração

Toda a parametrização fica em **`src/Config.h`**.

**Endpoint do stream** — HTTP puro, sem TLS. Em dev local, basta trocar host/porta:

```cpp
#define STREAM_HOST "192.168.15.38"
#define STREAM_PORT 8090
#define STREAM_PATH "/api/exam/stream"
```

**Credenciais WiFi:**

```cpp
#define WIFI_SSID     "..."
#define WIFI_PASSWORD "..."
```

<details>
<summary><b>Demais ajustes disponíveis</b></summary>

<br>

| Grupo | Constantes |
|---|---|
| Pinos | `LED_PIN_A` … `LED_PIN_E`, `LED_COUNT` |
| Saúde do stream | `STREAM_TIMEOUT_MS`, `STREAM_BACKOFF_TABLE`, `STREAM_HEAP_LOG_MS` |
| Buffers do parser | `SSE_MAX_LINE`, `SSE_MAX_DATA`, `SSE_MAX_CHUNK` |
| Conexão / ocioso / boot | `LED_CONN_BLINK_MS`, `LED_IDLE_PERIOD_MS`, `LED_IDLE_PULSE_MS`, `LED_BOOT_BLINK_MS` |
| Resposta retida | `LED_HOLD_TTL_MS`, `LED_HOLD_INTAKE_MS`, `LED_YESNO_BLINK_MS` |
| Processando | `LED_PROC_INDEX`, `LED_PROC_BLINK_MS` |
| Teste / erro / sequências | `LED_CHASE_*`, `LED_ERROR_*`, `LED_SEQ_*` |
| Serial e JSON | `SERIAL_BAUD_RATE`, `JSON_DOC_SIZE`, `JSON_FILTER_SIZE` |

O `monitor_speed` do `platformio.ini` precisa bater com `SERIAL_BAUD_RATE` (115200).

</details>

> [!NOTE]
> As credenciais WiFi ficam versionadas em texto plano por decisão do mantenedor.

---

## Build, upload e monitor

```bash
pio run                  # Compila o firmware
pio run --target upload  # Compila e grava no D1 Mini via USB (auto-detecta a COM)
pio device monitor       # Monitor serial (115200 baud)
pio run --target clean   # Limpa artefatos de build
```

Não há testes automatizados — é firmware embarcado, validado em hardware pelo Serial Monitor.
Para capturar o boot (que acontece antes de o monitor abrir), force um reset com o monitor já
conectado (toggle de DTR/RTS via pyserial).

---

## O stream

- **Método:** `GET {STREAM_HOST}:{STREAM_PORT}{STREAM_PATH}` → `Content-Type: text/event-stream`
- A conexão é **aberta uma vez e fica viva indefinidamente**; o servidor empurra eventos conforme ocorrem.
- Linhas iniciadas por `:` são comentários de prova de vida — `: connected` na abertura e `: ping` a cada 15 s.
- Apenas `answer` e `status` são tratados; qualquer outro nome de evento é descartado em silêncio.
- O corpo chega em `Transfer-Encoding: chunked` (o backend é Kestrel) e o firmware **desmonta os frames** antes de montar as linhas SSE.

Como o tráfego aparece na linha:

```text
: connected

event: status
data: {"state":"solving","activeSolves":1}

event: answer
data: {"hasData":true,"questionType":"single","letters":["C"],"answerText":"C"}

event: status
data: {"state":"idle","activeSolves":0}

: ping
```

**Sequência normal de um solve:** `status solving` → `answer` → `status idle`.
**Em falha:** `status solving` → `status error` (**sem** `answer`).
Ao abrir a conexão o servidor manda um `status` com o estado atual — reconectar no meio de um
processamento já traz `solving`.

<details>
<summary><b>Payload do evento <code>answer</code></b> (<code>SolverOutput</code>)</summary>

<br>

| Campo | Tipo | Significado |
|---|---|---|
| `hasData` | bool | `true` se a questão foi lida; `false` se ilegível ou modo Test |
| `questionType` | string | `single`, `multiple`, `yesno`, `dropdown`, `ordering`, `matching` ou `test` |
| `letters` | string[] | Letras A–E (`single` / `multiple`) |
| `flags` | bool[] | Sim/Não por afirmação (`yesno`) |
| `slots` | int[] | Posições 1–5 por slot (`dropdown` / `ordering` / `matching`) |
| `slotCount` | int | Nº de afirmações / lacunas / itens |
| `answerText` | string | Resposta legível (só vai para o log) |
| `explanation` | string | Justificativa — **ignorada no parse** |
| `elapsedMilliseconds` | long | Tempo de processamento no servidor |

O firmware lê apenas o necessário: um filtro do ArduinoJson descarta `explanation` (o campo mais
longo) antes da desserialização.

</details>

<details>
<summary><b>Payload do evento <code>status</code></b></summary>

<br>

| Campo | Tipo | Significado |
|---|---|---|
| `state` | string | `solving` (aguardando a IA), `idle` (ocioso) ou `error` (o processamento falhou) |
| `activeSolves` | int | Quantas requisições estão em andamento no servidor (informativo, só vai para o log) |

Um `state` desconhecido é tratado como `idle`, conforme a spec.

</details>

---

## A linguagem dos LEDs

Não há LED de status dedicado: a saúde da conexão e as respostas compartilham os mesmos 5 LEDs,
com padrões que não se confundem. **Resposta tem prioridade sobre conexão**, e um `answer` novo
sempre interrompe a exibição atual.

```
Notação:   ●  aceso fixo      ◐  piscando      ·  apagado
Ordem:     A  B  C  D  E   →  posições 1 a 5
```

### Conexão — quando não há resposta ativa

| Situação | Padrão | |
|---|:---:|---|
| Conectando / sem WiFi / reconectando | `◐ · · · ◐` | Pontas piscando juntas, ~150 ms |
| Conectado, ocioso | `◐ · · · ·` | Heartbeat: 1 pulso de ~80 ms a cada ~2 s |

### Processando — evento `status`

| `state` | Padrão | |
|---|:---:|---|
| `solving` | `· · ◐ · ·` | LED do meio piscando a ~250 ms, **sem TTL**, até chegar `answer`, `error` ou `idle`. Vence uma resposta em exibição e encerra a janela de silêncio do boot |
| `error` | `◐ ◐ ◐ ◐ ◐` | Padrão de erro: os 5 piscam juntos 3× e voltam ao ocioso |
| `idle` | — | Encerra o piscar do meio. **Não apaga uma resposta em exibição** — no fluxo normal o `idle` chega logo após o `answer` |

Se o stream cair durante um `solving`, o piscar é **abortado** e os LEDs voltam a sinalizar a
conexão — do contrário a queda ficaria escondida. Ao reabrir, o `status` inicial ressincroniza.

### Resposta — evento `answer`

| Situação | Exemplo | |
|---|:---:|---|
| `single` | `· · ● · ·` | 1 LED aceso (a letra), **retido por 12 s** (`LED_HOLD_TTL_MS`), depois volta ao heartbeat |
| `multiple` | `● · ● · ·` | LEDs das letras acesos simultaneamente, mesmo TTL |
| `yesno` | `● ◐ ● · ·` | **Simultâneo**: cada afirmação acende seu LED — Sim = fixo, Não = piscando (~350 ms) — mesmo TTL |
| `dropdown` `ordering` `matching` | `· ● · · ·` → `· · · ● ·` → … | **Sequencial**: acende a posição (1–5) de cada slot, na ordem, em 2 passadas |
| `questionType == "test"` | `● → ● → ● → ● → ●` | Varredura (chase) A→E, 2×, e volta ao ocioso |
| `hasData == false` (ilegível) | `◐ ◐ ◐ ◐ ◐` | Os 5 piscam juntos 3× (~250 ms on/off) e apagam |
| Erro ao parsear o JSON | `◐ ◐ ◐ ◐ ◐` | Mesmo padrão de erro — em vez de falhar em silêncio |

Toda resposta retida começa com um **blank de ~250 ms** (`LED_HOLD_INTAKE_MS`), o que torna
visível a chegada de respostas **iguais consecutivas** (ex.: `A` depois `A`).
Sequências com mais de 5 itens são truncadas para 5, com aviso na serial; slot fora de 1–5 pisca
os 5 juntos 1× naquele passo e segue.

### Janela de boot — silêncio até a 1ª resposta

Ao ligar, os LEDs sinalizam conexão/ocioso normalmente por **`LED_BOOT_BLINK_MS` (5 min)**;
depois entram em **blackout** (`· · · · ·`) até a **1ª resposta** do backend.

Durante o blackout o stream continua ativo — ping a cada 15 s e logs na serial — **só a saída dos
LEDs é suprimida**. Qualquer evento `answer` (inclusive `test` ou erro) **ou um `status: solving`**
encerra o blackout **em definitivo**: ele não rearma. Se a 1ª resposta chegar antes dos 5 min, o
blackout nunca acontece.

---

## Reconexão automática

Reconecta se **(a)** o socket cair, **(b)** o WiFi cair, ou **(c)** passar `STREAM_TIMEOUT_MS`
(~40 s) sem nenhuma linha.

```
1 s → 2 s → 5 s → 10 s → 20 s → 30 s (máx)
```

O backoff zera quando o stream reabre. Durante a reconexão, os LEDs mostram `◐ · · · ◐`.

---

## Testes / critérios de aceite

> Os `POST` abaixo são disparados **de um PC**, apenas para gerar eventos no stream — nada disso roda no ESP.

| # | Cenário | Esperado |
|:--:|---|---|
| 1 | **Conexão viva** | Serial mostra `[SSE] Stream aberto`; `: ping` a cada 15 s sem reconectar; heartbeat no LED A quando ocioso |
| 2 | **Evento de teste** (sem custo de IA) | `POST {BASE}/api/exam/solve` com `Test=true` (multipart) → chase A→E em < ~1 s |
| 3 | **`single` / `multiple`** | 1 LED / vários LEDs acesos e retidos por ~12 s |
| 4 | **`yesno`** | LEDs das afirmações acesos ao mesmo tempo — Sim fixo, Não piscando |
| 5 | **Processando (`status`)** | `POST` real (sem `Test`) → o LED do meio pisca imediatamente e durante todo o processamento; ao chegar o `answer`, ele para e a resposta aparece; o `idle` seguinte **não** apaga a resposta. Se falhar, chega `status error` → 5 LEDs piscando 3× |
| 6 | **`dropdown` / `ordering` / `matching`** | Sequência acendendo a posição de cada slot |
| 7 | **Ilegível** | Force `hasData=false` → 5 LEDs piscando juntos 3× |
| 8 | **Reconexão** | Derrube WiFi/servidor → padrão `◐ · · · ◐` + log de backoff; ao voltar, reconecta e o backoff zera |
| 9 | **Heap** | Acompanhe `[HEAP] livre=` (a cada 10 s) — deve permanecer estável após muitos eventos e reconexões |

---

## Solução de problemas

| Sintoma | Provável causa |
|---|---|
| WiFi não conecta | SSID/senha incorretos, ou rede em 5 GHz — o ESP8266 só suporta 2,4 GHz |
| `[SSE] status HTTP != 200` ou reconexão constante | `STREAM_HOST` / `STREAM_PORT` / `STREAM_PATH` apontando para o lugar errado, ou servidor inacessível na rede |
| Erro de build (`ESP8266WiFi.h`, `ArduinoJson.h`) | Rode `pio run` — o PlatformIO baixa a plataforma e as dependências automaticamente |
| LEDs apagados, mas a serial mostra atividade | Janela de boot: são os 5 min de blackout aguardando a 1ª resposta ([detalhes](#janela-de-boot--silêncio-até-a-1ª-resposta)) |
| `showError()` em toda resposta longa | Payload truncado — verifique `SSE_MAX_LINE` / `SSE_MAX_DATA` e o de-framing chunked |

---

## Histórico de versões

| Versão | Mudança |
|:---:|---|
| **2.4** | Evento `status` → LED de processamento (`solving` / `idle` / `error`) |
| **2.3** | De-framing do `Transfer-Encoding: chunked`; todo header de resposta passa a ser logado |
| **2.2** | Janela de silêncio no boot: LEDs apagados após 5 min, até a 1ª resposta |
| **2.1** | Parse JSON zero-copy (`char*`) — corrige respostas longas que não acendiam os LEDs |
| **2.0** | Polling HTTPS substituído pelo stream SSE persistente; reconexão com backoff |
| **1.0** | Versão inicial por polling |

O changelog completo, com o diagnóstico de cada correção, está no topo de **`src/main.cpp`**.

---

## Recursos

- [Documentação do ESP8266 Arduino core](https://arduino-esp8266.readthedocs.io/)
- [Pinout do D1 Mini](https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/)
- [ArduinoJson](https://arduinojson.org/)
- [Especificação SSE (WHATWG)](https://html.spec.whatwg.org/multipage/server-sent-events.html)

<div align="center">
<br>
<sub>Proera · 2026</sub>
</div>
