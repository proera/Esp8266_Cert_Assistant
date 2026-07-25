# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Visão geral

Firmware para D1 Mini (ESP8266) que consome o stream **SSE** da API CertMind por **uma única conexão HTTP persistente** (GET, texto claro, sem TLS) e aciona **5 LEDs (A–E / posições 1–5)** conforme cada situação emitida pelo backend. O servidor difunde **dois eventos**: `answer` (a resposta resolvida) e `status` (andamento do processamento: `solving`/`idle`/`error`). O ESP é apenas consumidor: abre o GET e escuta; não faz POST nem envia imagem. Projeto **PlatformIO**, ambiente único `[env:d1_mini]`.

## Comandos

```bash
pio run                 # Compila o firmware
pio run --target upload # Compila e grava no D1 Mini via USB (auto-detecta a COM)
pio device monitor      # Monitor serial (115200 baud)
pio run --target clean  # Limpa artefatos de build
```

Não há testes automatizados — é firmware embarcado validado em hardware via Serial Monitor. Para capturar o boot (que ocorre antes de o monitor abrir), force um reset com o monitor já conectado (toggle DTR/RTS via pyserial).

## Arquitetura

Modular, uma responsabilidade por arquivo:
- `src/Config.h` — todas as constantes via `#define` (WiFi, endpoint do stream, pinos, timings dos padrões de LED, backoff, tetos de buffer).
- `src/WiFiManager.{h,cpp}` — conexão WiFi inicial (modo STA) + helpers de status.
- `src/SseClient.{h,cpp}` — abre o GET, valida headers, de-framing chunked, parser SSE não-bloqueante linha-a-linha, reconexão com backoff. Ao fim de um evento `answer` ou `status`, chama o callback correspondente com o payload; outros nomes de evento são descartados.
- `src/LedController.{h,cpp}` — máquina de estados dos 5 LEDs, **toda em `millis()`** (zero `delay()`).
- `src/main.cpp` — liga tudo; `handleAnswer()` decide o padrão de LED por `hasData` + `questionType`, e `handleStatus()` decide por `state`.

Fluxo: `SseClient` recebe `answer`/`status` → `handleAnswer()`/`handleStatus()` parseia → decide → `LedController` exibe.

## Pontos críticos (gotchas)

- **Parsing JSON é zero-copy — NÃO mude o callback para `const char*`.** `SseClient::AnswerCallback` é `void(*)(char*)` e `handleAnswer(char* payload)` passa o buffer **mutável** (`_dataBuf`) a `deserializeJson`. Isso ativa o `StringMover` do ArduinoJson (strings *Linked*, sem cópia no pool: ~160/512 bytes). Se voltar a `const char*`, o ArduinoJson copia as strings e um `answerText` longo (ex.: `matching`/`ordering` com setas/acentos UTF-8) **estoura** o `StaticJsonDocument<JSON_DOC_SIZE>` → `NoMemory` → LEDs não acendem. Foi o bug corrigido na v2.1. As strings extraídas (ex.: `answerText` para log) só são válidas **dentro** de `handleAnswer`, pois o buffer é reescrito no próximo evento.
- **Filtro do ArduinoJson** (`g_filter` em `main.cpp`) parseia só os campos usados + `answerText` (log) e **ignora `explanation`** (campo mais longo).
- **Evento `status` = LED de processamento (v2.4).** `solving` → `showProcessing()` faz o **LED C (índice `LED_PROC_INDEX`) piscar** a `LED_PROC_BLINK_MS` **sem TTL**, até chegar `answer`/`error`/`idle`; vence resposta segurada. `error` → `showError()`. `idle` → `stopProcessing()`, que **só age se o modo corrente é `MODE_PROCESSING`** — é isso que impede o `idle` (que chega logo após o `answer` no fluxo normal) de apagar a resposta exibida; não trocar por um `allOff()` incondicional. `state` desconhecido cai no mesmo caminho do `idle` (spec). Como o processando não tem TTL, `setConnected(false)` **aborta** o modo — senão o LED C piscaria para sempre escondendo a queda do stream; ao reabrir, o `status` inicial do servidor ressincroniza. Erro de parse do `status` **não** aciona `showError()` (ao contrário do `answer`): o padrão de erro é reservado às respostas, e piscar os 5 aqui seria lido como "questão ilegível" — fica só no log `[JSON]`.
- **`LedController` é não-bloqueante e prioriza resposta sobre conexão.** `single`/`multiple`/`yesno` ficam em **HOLD por `LED_HOLD_TTL_MS`** (12 s) e então voltam ao heartbeat; um novo `answer` sempre interrompe. `test`/erro/sequências (`dropdown`/`ordering`/`matching`) **tocam e voltam ao ocioso**. Toda exibição HOLD começa com um **blank de `LED_HOLD_INTAKE_MS`** (~250 ms tudo apagado) para tornar visível a chegada de respostas **iguais consecutivas** (ex.: `A` depois `A`) — sem ele, dois answers idênticos seriam indistinguíveis. O `renderHold()` usa `_animStart` p/ o TTL/blank e mantém duas máscaras (`_holdMask` fixo + `_holdBlinkMask` piscando); no `yesno` cada afirmação acende seu LED simultaneamente (**Sim = fixo, Não = piscando**, `LED_YESNO_BLINK_MS`), e `single`/`multiple` zeram a de piscar (estáticos). Sem resposta ativa: conexão sinalizada nos mesmos 5 LEDs (pontas A+E piscando = conectando/reconectando; pulso curto em A = ocioso).
- **Janela de boot (v2.2).** Após `begin()`, o `LedController` exibe os padrões de conexão/ocioso normalmente por `LED_BOOT_BLINK_MS` (5 min); depois entra em **blackout** (LEDs apagados via `allOff()` no início do ramo `!_answerActive` de `update()`) **até a 1ª resposta**. O SSE e os logs na serial **seguem ativos** no blackout — só a saída dos LEDs é suprimida. Qualquer evento `answer` (single/multiple/yesno/sequência/`test`/erro) **ou um `status: solving`** marca `_firstAnswerReceived = true` (setado nos cinco pontos que ativam `_answerActive`: `startHold`/`startSeq`/`showTestChase`/`showError`/`showProcessing`) e **encerra o blackout em definitivo** — ele não rearma. Se a 1ª resposta chegar antes dos 5 min, o blackout nunca ocorre. O gate só roda quando não há resposta ativa, então nunca interfere na exibição de uma resposta.
- **Erro de parse aciona `leds.showError()`** (não falha em silêncio) — o `return` silencioso era o que escondia bugs como o da v2.1.
- **O corpo do SSE vem em `Transfer-Encoding: chunked` e o `SseClient` desmonta os frames (v2.3) — não remover essa camada.** O backend (Kestrel) prefixa cada bloco com o tamanho em hex e o sufixa com CRLF, então o que chega na conexão **não** é o corpo lógico. `feedChunkedByte()` consome size/CRLF e passa a `feedLine()` só os bytes de dados; `beginChunkSize()` rearma o de-framing em `begin()`/`tryConnect()`/`closeConnection()` e ao entrar em `ST_STREAMING`. Sem isso, o CRLF terminador de um chunk que caia **no meio de uma linha `data:`** corta a linha ali → payload truncado → `showError()`; funcionava por acidente porque o Kestrel costuma emitir 1 chunk por evento. Uma linha de chunk-size **sem nenhum dígito hex** é ruído/trailer e só rearma (tratá-la como tamanho 0 dispararia um retry extra e pularia um degrau do backoff). O caminho identity (servidor sem chunked) segue funcionando: `_chunked` é detectado nos headers.
- **Todo header de resposta é logado (`[SSE] < ...`)** — não reduzir de volta a "só a linha de status". Foi justamente a ausência desse log que manteve a falta do de-framing chunked invisível.
- **Sem TLS**: usa `WiFiClient` puro e monta o GET manualmente. Não reintroduzir `HTTPClient` (bloqueia esperando o corpo terminar, incompatível com stream infinito) nem `WiFiClientSecure`.

## Configuração

Toda parametrização fica em **`src/Config.h`**. Em dev local, trocar `STREAM_HOST`/`STREAM_PORT`/`STREAM_PATH`. O `monitor_speed` em `platformio.ini` deve bater com `SERIAL_BAUD_RATE` (115200).

## Restrições

- **NÃO alterar as credenciais WiFi em `src/Config.h`** — ficam versionadas em texto plano por decisão do mantenedor; manter como estão.
- ESP8266 só opera em WiFi **2,4 GHz**.
