# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Visão geral

Firmware para D1 Mini (ESP8266) que consome o stream **SSE** da API CertMind por **uma única conexão HTTP persistente** (GET, texto claro, sem TLS) e aciona **5 LEDs (A–E / posições 1–5)** conforme cada situação emitida pelo backend. O ESP é apenas consumidor: abre o GET e escuta; não faz POST nem envia imagem. Projeto **PlatformIO**, ambiente único `[env:d1_mini]`.

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
- `src/SseClient.{h,cpp}` — abre o GET, valida headers, parser SSE não-bloqueante linha-a-linha, reconexão com backoff. Ao fim de um evento `answer`, chama o callback com o payload.
- `src/LedController.{h,cpp}` — máquina de estados dos 5 LEDs, **toda em `millis()`** (zero `delay()`).
- `src/main.cpp` — liga tudo; `handleAnswer()` parseia o JSON e decide o padrão de LED por `hasData` + `questionType`.

Fluxo: `SseClient` recebe evento `answer` → `handleAnswer()` parseia → decide → `LedController` exibe.

## Pontos críticos (gotchas)

- **Parsing JSON é zero-copy — NÃO mude o callback para `const char*`.** `SseClient::AnswerCallback` é `void(*)(char*)` e `handleAnswer(char* payload)` passa o buffer **mutável** (`_dataBuf`) a `deserializeJson`. Isso ativa o `StringMover` do ArduinoJson (strings *Linked*, sem cópia no pool: ~160/512 bytes). Se voltar a `const char*`, o ArduinoJson copia as strings e um `answerText` longo (ex.: `matching`/`ordering` com setas/acentos UTF-8) **estoura** o `StaticJsonDocument<JSON_DOC_SIZE>` → `NoMemory` → LEDs não acendem. Foi o bug corrigido na v2.1. As strings extraídas (ex.: `answerText` para log) só são válidas **dentro** de `handleAnswer`, pois o buffer é reescrito no próximo evento.
- **Filtro do ArduinoJson** (`g_filter` em `main.cpp`) parseia só os campos usados + `answerText` (log) e **ignora `explanation`** (campo mais longo).
- **`LedController` é não-bloqueante e prioriza resposta sobre conexão.** `single`/`multiple` ficam em **HOLD** (acesos até o próximo `answer`); `test`/erro/sequências (`yesno`/`dropdown`/`ordering`/`matching`) **tocam e voltam ao ocioso** (não ficam acesas). Sem resposta ativa: conexão sinalizada nos mesmos 5 LEDs (pontas A+E piscando = conectando/reconectando; pulso curto em A = ocioso).
- **Erro de parse aciona `leds.showError()`** (não falha em silêncio) — o `return` silencioso era o que escondia bugs como o da v2.1.
- **Sem TLS**: usa `WiFiClient` puro e monta o GET manualmente. Não reintroduzir `HTTPClient` (bloqueia esperando o corpo terminar, incompatível com stream infinito) nem `WiFiClientSecure`.

## Configuração

Toda parametrização fica em **`src/Config.h`**. Em dev local, trocar `STREAM_HOST`/`STREAM_PORT`/`STREAM_PATH`. O `monitor_speed` em `platformio.ini` deve bater com `SERIAL_BAUD_RATE` (115200).

## Restrições

- **NÃO alterar as credenciais WiFi em `src/Config.h`** — ficam versionadas em texto plano por decisão do mantenedor; manter como estão.
- ESP8266 só opera em WiFi **2,4 GHz**.
