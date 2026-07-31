# Plano de migração — ESP8266 D1 Mini → ESP32-S3 Super Mini

> **Status: em execução.** M0, M1 e M2 concluídos (M1 validado em hardware: stream,
> ping, heartbeat e test chase ok; restam os padrões de resposta reais e reconexão);
> decisões da seção 6 fechadas em 31/07/2026. A numeração de marcos vigente é a da
> seção 6 (a seção 4 mantém a numeração original, anterior às decisões).

---

## 1. Contexto

O firmware v2.4 é maduro e cada decisão de design nele tem uma cicatriz por trás
(v2.1 zero-copy, v2.3 de-framing chunked, v2.4 `stopProcessing()` condicional).
Ele também está **encostado no teto do hardware**: 46,9 % da RAM do ESP8266 está
comprometida (38 412 de 81 920 B), e **~9 KB disso — 23 % de toda a RAM estática —
são apenas os dois buffers de 4 KB do parser SSE e os dois documentos JSON.**

Três restrições estruturais decorrem daí, todas documentadas como proibições em
`ARCHITECTURE.md` §10:

1. **Sem TLS** — `WiFiClientSecure` com BearSSL pede 16–22 KB de heap por handshake;
   não cabe junto dos 8960 B de buffers.
2. **`explanation` inacessível** — o `StaticJsonDocument<512>` só cabe com um filtro
   que descarta o campo mais longo. O backend chegou a **remover o campo do contrato**
   na v2.0.0 justamente porque o ESP nunca conseguiria usá-lo.
3. **Contrato zero-copy frágil** — o `char*` de `AnswerCallback` é obrigatório, não
   opcional; trocar por `const char*` reintroduz silenciosamente o bug da v2.1.

O ESP32-S3 remove as três de uma vez. O objetivo deste plano é fazer isso **sem
perder nenhuma das cicatrizes** — o valor do firmware atual está nos invariantes que
ele aprendeu do jeito difícil.

---

## 2. O que se ganha, em números

Números do ESP8266 medidos com `pio run` no commit `cfc4ca4`; os do S3 medidos no
projeto de teste em `C:\Projetos\Esp32S3_SuperMini_Test`, gravado e validado na placa.

| | **ESP8266 D1 Mini** | **ESP32-S3 Super Mini (FH4R2)** |
|---|---|---|
| CPU | 80 MHz, 1 núcleo | **240 MHz, 2 núcleos** (Xtensa LX7) |
| RAM | 80 KB — **46,9 % ocupada** (38 412 B) | 512 KB on-chip; build reporta 19 072 / 327 680 B (**5,8 %**), runtime reportou **396 KB de heap** com 371 KB livres |
| PSRAM | — | **2 MB**, testados com escrita/leitura de 1 MB, 0 erros |
| Flash | 4 MB, partição de app 1,02 MB, **sem OTA** | 4 MB, **2 × 1,31 MB de app → OTA disponível** |
| TLS | inviável (BearSSL vs. heap) | **mbedTLS com aceleração de AES/SHA/RSA em hardware** |
| Documento JSON | 512 B + filtro obrigatório | livre — **`explanation` volta a caber** |
| Buffers SSE | 4096 B; evento > 4 KB é **descartado em silêncio** | 16 KB+ em SRAM, ou arbitrário em PSRAM |
| PWM | não usado (só `digitalWrite`) | **LEDC, 8 canais** → fade e dimming |
| Persistência | nenhuma (tudo `#define`) | **NVS** via `Preferences` |
| Depuração | só `Serial.print` | **JTAG nativo por USB** (`debug_tool = esp-builtin`) — breakpoints reais |
| Serial | adaptador CH340 externo | **USB-Serial/JTAG nativo** no mesmo USB-C |
| Extra | — | **WS2812 RGB onboard** (GPIO 48), 14 canais de touch capacitivo |

O ganho que eu classificaria como o mais subestimado dessa tabela é o **JTAG**. Hoje
`CLAUDE.md` registra que "não há testes automatizados — é firmware embarcado validado
em hardware via Serial Monitor". Bugs como o da v2.3 (chunk cortando uma linha `data:`
no meio) levaram meses para aparecer porque a única janela para dentro do firmware era
o log. Com breakpoint e watch de variável, esse bug se enxerga em minutos.

---

## 3. Estratégia

### 3.1. Princípio: porte mecânico primeiro, ganhos depois

O erro clássico aqui é misturar "trocar de plataforma" com "aproveitar a plataforma
nova" no mesmo passo — quando algo quebra, não se sabe qual das duas coisas causou.
O plano é: **M1 reproduz o comportamento atual byte a byte**, e só então cada ganho
entra como um marco isolado e reversível.

### 3.2. O que porta praticamente sem tocar

| Módulo | Esforço | Observação |
|---|---|---|
| `LedController.{h,cpp}` | **quase zero** | É 100 % `millis()` + `digitalWrite`. Só muda o mapa de pinos. |
| `SseClient.{h,cpp}` | **baixo** | `WiFiClient` tem a mesma API no ESP32. Todo o parser de headers, o de-framing chunked e a máquina de estados portam **como estão**. |
| `WiFiManager.{h,cpp}` | **baixo** | `ESP8266WiFi.h` → `WiFi.h`; `WiFi.mode`/`begin`/`status`/`localIP`/`setAutoReconnect` são idênticos. |
| `main.cpp` (lógica de decisão) | **zero** | O mapeamento `questionType` → padrão de LED não depende de plataforma. |

### 3.3. O que obrigatoriamente muda

**a) Mapa de pinos — e um bug latente a corrigir.**
Hoje `LED_PIN_A` é `D3` = **GPIO0**, que no ESP8266 é pino de boot-strapping. No S3,
**GPIO0 também é strapping** (é o botão BOOT). Repetir o mapeamento reintroduz o
problema. Pinos a evitar no S3:

| Pino | Motivo |
|---|---|
| GPIO 0 | strapping / botão BOOT |
| GPIO 19, 20 | USB D-/D+ — usá-los derruba a serial e o upload |
| GPIO 26–32 | barramento da flash SPI embarcada |
| GPIO 33–37 | reservados em módulos com PSRAM octal (no FH4R2 a PSRAM é quad, mas a Espressif recomenda cautela) |
| GPIO 45, 46 | strapping (VDD_SPI e log de boot) |
| GPIO 48 | WS2812 onboard |

Candidatos limpos para os 5 LEDs: **GPIO 1, 2, 3, 4, 5** — a confirmar contra a
serigrafia da Super Mini antes de soldar.

**b) `yield()` deixa de significar o que significava.**
No ESP8266 o `yield()` é o que devolve a CPU ao scheduler cooperativo e alimenta o WDT
— e o firmware depende disso em dois pontos (`SseClient.cpp:203` e `main.cpp:309`).
Sob FreeRTOS no S3, isso vira `vPortYield()` e **não** tem a mesma semântica. O porte
precisa trocar por `vTaskDelay(1)` ou por um tick de task; não é opcional.

**c) `WiFi.setSleep(false)`.**
O ESP32 liga modem sleep por padrão, o que injeta dezenas a centenas de ms de jitter
na recepção. Num stream cuja latência é o ponto do projeto, isso tem de ser desligado
explicitamente — é uma linha, e não existe equivalente no ESP8266.

### 3.4. O que **não** mexer (invariantes a preservar)

Cada item abaixo custou um incidente. O porte precisa carregá-los intactos:

1. **De-framing chunked** (`feedChunkedByte`/`beginChunkSize`) — sem ele o payload
   volta a truncar de forma intermitente.
2. **Erro de parse do `answer` → `showError()`** — nunca voltar ao `return` silencioso.
3. **Erro de parse do `status` → só log** — piscar os 5 aqui seria lido como
   "questão ilegível".
4. **`stopProcessing()` condicional ao `MODE_PROCESSING`** — um `allOff()`
   incondicional apaga a resposta recém-exibida.
5. **HOLD com TTL + blank de intake de 250 ms** — é o que torna visível duas
   respostas iguais consecutivas.
6. **Blackout de boot que não rearma.**
7. **Log de *todo* header** (`[SSE] < ...`) — a ausência dele foi o que manteve o bug
   da v2.3 invisível.
8. **Zero `delay()` no caminho de renderização.**

### 3.5. A decisão de arquitetura: dual-core

O problema concreto de hoje: `_client.connect()` (`SseClient.cpp:80`) bloqueia por até
~5 s e **congela a animação dos LEDs**; `wifiManager.connect()` congela até 15 s no
boot; e o `Serial.print` de cada header bloqueia quando o buffer de TX enche.

Desenho proposto:

```
  netTask  (core 0, prioridade normal)      uiTask  (core 1, tick de 5 ms)
  ─────────────────────────────────────     ──────────────────────────────
  sse.loop()            ← pode bloquear     leds.update()   ← nunca bloqueia
  parseia o JSON
  monta LedCommand  ──── xQueueSend ───→    xQueueReceive → aplica no LedController
```

O ponto crítico do desenho: **a fila carrega um `struct LedCommand` por valor**
(modo + máscaras + parâmetros já decodificados), nunca um ponteiro para o `_dataBuf`.
Isso respeita o contrato zero-copy — o buffer é reescrito no próximo evento, então um
ponteiro atravessando a fila seria um use-after-write clássico. O `LedController`
passa a ser tocado **exclusivamente** pela `uiTask`, o que dá um invariante simples de
verificar em code review.

**Risco honesto:** essa é a maior fonte de bugs *novos* do porte inteiro (corridas em
estado compartilhado). Por isso ela é o marco **M3**, e não parte do M1 — o M1 mantém
o `loop()` único e já ganha 3× de clock. Se o M3 der problema, M1/M2 seguem em pé.

### 3.6. TLS: é uma mudança de dois lados

O S3 torna o TLS viável no cliente, mas **o backend não tem HTTPS configurado**: não
existe seção `Kestrel` no projeto, `UseHttpsRedirection()` em `Program.cs:81` é
inócuo porque o container publica só a 8080, e a doc do firmware afirma
explicitamente *"O deploy não terá HTTPS"*. Portanto:

- **TLS puro no firmware não resolve nada sozinho** — precisa de certificado no
  Kestrel ou de um proxy reverso (Caddy/nginx) terminando TLS na frente.
- **Ganho imediato e independente:** a API hoje **não tem autenticação nenhuma**
  (`Program.cs:82` chama `UseAuthorization()` sem esquema registrado — é no-op), e
  qualquer host da LAN pode disparar solves que consomem crédito do Foundry. Um
  header `Authorization: Bearer` no GET é **uma linha** em `tryConnect()`.
  Ressalva: token sobre HTTP em claro é capturável na rede — melhora o controle de
  acesso, não a confidencialidade.

Por isso TLS é o último marco e está marcado como **dependente do backend**.

---

## 4. Marcos

Cada marco é autocontido e reversível por `git revert`.

### M0 — Baseline de toolchain ✅ *concluído*
Projeto de teste em `C:\Projetos\Esp32S3_SuperMini_Test`, gravado e validado: board
`esp32-s3-devkitc-1` com overrides de 4 MB, partições `default.csv`, PSRAM quad
(`qio_qspi` + `-DBOARD_HAS_PSRAM`), serial pelo USB-C
(`-DARDUINO_USB_CDC_ON_BOOT=1`). Chip, flash, PSRAM e RGB confirmados em hardware.

### M1 — Porte mecânico, comportamento idêntico ✅ *código concluído (v3.0); validação em hardware pendente*
- Novo env `esp32s3_supermini`, `WiFi.h`, barra WS2812 no GPIO 13 (pixels 0–4 = A–E
  com as cores do D1, pixels 5–7 apagados), `yield()` → `delay(1)`, `setSleep(false)`.
- Buffers, tetos, filtro JSON e contrato `char*` **sem alteração**.
- **Build registrado:** RAM 16,7 % (54 588 / 327 680 B) — contra 46,9 % (38 412 / 81 920 B)
  no 8266; Flash 55,7 % (729 745 / 1 310 720 B) no slot de app com OTA disponível.
- **Aceite:** contra o mesmo backend, os 9 critérios do `README.md` passam com
  comportamento visualmente idêntico ao D1 Mini. *(Pendente: gravar na placa e validar.)*

### M2 — Destravar memória ✅ *concluído (v3.1)*
- `SSE_MAX_LINE`/`SSE_MAX_DATA`: 4096 → 16384. Fim do descarte silencioso de evento.
- `JSON_DOC_SIZE`: 512 → 4096; **`g_filter` removido**.
- Assinatura `char*` mantida (custa nada e segue correta); nada no código atual
  precisa sobreviver ao evento — quando precisar, copiar com `strlcpy`.
- Uso do pool logado a cada `answer` (`[JSON] pool=`) — visibilidade antes de um
  eventual `NoMemory`.
- **Build:** RAM 25,2 % (82 452 B; +27,9 K sobre o M1), Flash 55,6 % — folga ampla.
- **Aceite:** payloads até ~16 K deixam de ser descartados (teto anterior: 4 K);
  validar com um solve real observando o `[JSON] pool=` no monitor.

### M3 — Split dual-core
Conforme §3.5. **Aceite:** forçar `connect()` a falhar (backend derrubado) e confirmar
que a animação de "reconectando" **não engasga** — hoje ela congela por ~5 s.

### M4 — RGB onboard assume o status *(superado pela decisão 2 da seção 6)*
A ideia original era o WS2812 onboard (GPIO 48) carregar conexão/processamento em cor.
Com a barra de 8 pixels no GPIO 13, o mesmo objetivo é atingido melhor: **2 pixels da
própria barra** viram status e sobra espaço para a **6ª posição de resposta (F)** —
é o novo M3 da seção 6. O compromisso documentado (*"a saúde da conexão e as respostas
compartilham os mesmos 5 LEDs"*) cai do mesmo jeito, e a escada de prioridades de
`LedController.h` simplifica.

### M5 — Robustez (dois bugs reais + protocolo)
- **Travamento indefinido em `ST_HEADERS`**: `checkTimeout()` só é chamado no ramo
  `ST_STREAMING` (`SseClient.cpp:61`). Num half-open (servidor aceita o TCP e nunca
  responde, sem FIN), `_client.connected()` segue `true` e o firmware fica **preso
  para sempre** com os LEDs em "conectando". É a lacuna mais concreta do código atual.
- **Detecção de status HTTP frágil** (`SseClient.cpp:323`): um `HTTP/1.1 200` sem
  reason-phrase é rejeitado como não-200.
- `Last-Event-ID` + honrar `retry:` — hoje `id:`/`retry:` são ignorados
  (`SseClient.cpp:391`) e todo evento emitido durante a janela de reconexão
  (1–30 s) é **perdido em definitivo**.
- Header `Authorization: Bearer`.

### M6 — OTA + configuração em NVS
- OTA nas duas partições de app: hoje qualquer mudança exige acesso físico.
- Mover host/porta/path e credenciais para NVS (`Preferences`). Resolve de passagem o
  problema do **IP literal** em `Config.h:28` — uma troca de DHCP hoje quebra o
  firmware até recompilar.
- **Higiene de segredo, independente do marco:** a senha do WiFi está em
  `Config.h:18-19` versionada em texto plano **e** no histórico via o
  `plans/plano_detalhado.md` apagado no commit `198bd40`. Remover do arquivo atual
  não resolve — a rotação da senha da rede é a única mitigação eficaz.

### M7 — TLS *(bloqueado por dependência do backend)*
`WiFiClientSecure` + pinning. Só faz sentido depois que o Kestrel tiver certificado
ou houver proxy reverso terminando TLS.

---

## 5. Riscos e pontos de atenção

| Risco | Mitigação |
|---|---|
| Corridas no split dual-core | Fila por valor; `LedController` tocado só pela `uiTask`; marco isolado e reversível |
| `yield()` com semântica diferente sob FreeRTOS | Tratado explicitamente no M1, não deixado para depois |
| Brownout por cabo/fonte USB fraca | O S3 puxa picos bem maiores que o 8266 em TX. O relatório de diagnóstico do projeto de teste já imprime o motivo do reset — `brownout` aparece nomeado |
| Orçamento de flash com TLS + OTA | mbedTLS cabe em 1,31 MB por slot; o firmware de teste com PSRAM e diagnóstico ficou em 275 KB. Folga confortável |
| ArduinoJson 6 vs 7 | **Manter 6.21.x no porte.** A 7.x troca documentos estáticos por alocação dinâmica; mudar de major junto com a plataforma é variável a mais sem ganho |
| Log de boot não aparece no monitor | O bootloader ROM do S3 não fala pelo USB-CDC. O fluxo de captura muda: reset com o monitor já aberto (pulso no RTS) |
| Temperatura interna aparente alta | `temperatureRead()` no core 2.0.x do S3 satura perto de 80 °C — a leitura não é confiável, e o diagnóstico já avisa quando satura |

---

## 6. Decisões (fechadas em 31/07/2026)

1. **Repositório: substituição do alvo 8266.** O env `d1_mini` sai e entra
   `esp32s3_supermini` neste mesmo repo. O D1 Mini é aposentado — histórico e
   cicatrizes ficam preservados no git.
2. **LEDs: barra WS2812 de 8 pixels no GPIO 13** (já conectada e validada no M0),
   no lugar dos 5 LEDs discretos e do RGB onboard. Desenho-alvo: **6 pixels de
   resposta (A–F) + 2 pixels dedicados a status** (conexão/processamento) — isso
   substitui o antigo M4 (RGB onboard) e vira o novo M3. No M1 o comportamento é
   reproduzido identicamente: 5 posições nos pixels 0–4, pixels 5–7 apagados.
3. **Cores dos pixels de resposta = cores dos LEDs físicos do D1**: A=verde,
   B=amarelo, C=vermelho, D=azul, E=branco; F (novo, M3) = magenta.
4. **D1 Mini não fica em produção em paralelo** (decorrência da decisão 1) — o
   contrato SSE só precisa servir ao S3 a partir do M1.
5. **`explanation` volta ao contrato do backend?** Segue em aberto: foi removido na
   v2.0.0 do CertMind por custo de token. O M2 destrava o lado do firmware, mas o
   campo só volta a existir se o backend o reemitir — e num simulado ele é o dado
   mais útil que há.

### Marcos renumerados após as decisões

- **M1** — porte mecânico (comportamento idêntico; 5 posições nos pixels 0–4).
- **M2** — destravar memória (buffers 16 K, `JSON_DOC_SIZE` 4096, sem filtro).
- **M3** — LEDs 6+2: `LED_COUNT` 5→6, letras A–F, F=magenta, pixels 6–7 assumem
  conexão/processamento. A escada de prioridades simplifica: `stopProcessing()` e o
  aborto por queda de stream são revistos porque status deixa de disputar pixel com
  resposta.
- **M4** — split dual-core (antigo M3).
- **M5** — robustez restante: `Last-Event-ID` + `retry:`, header `Authorization`.
  *(O timeout de headers half-open e o parse numérico do status HTTP, da lista
  original do M5, já entraram na v2.5 do firmware 8266, antes do porte.)*
- **M6** — OTA + NVS. **M7** — TLS (bloqueado pelo backend).
