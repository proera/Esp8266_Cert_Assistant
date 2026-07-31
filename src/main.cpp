/*
 * Sistema CertMind - Cliente de Stream ESP32-S3 (Super Mini)
 *
 * Versão: 3.4
 *
 * Descrição: Consome o stream SSE da API CertMind por UMA conexão HTTP
 * persistente (GET, texto claro, sem TLS) e aciona os LEDs da barra WS2812
 * conforme cada situação emitida pelo backend (test, ilegível, single,
 * multiple, yesno, dropdown, ordering, matching), o andamento do
 * processamento no servidor (evento status) e a saúde da conexão.
 *
 * O ESP32 é apenas consumidor: abre o GET e fica escutando. Não faz POST,
 * não envia imagem, não faz request/response.
 *
 * Hardware: ESP32-S3 Super Mini (FH4R2) + barra WS2812 de 8 pixels no
 * GPIO 13. Pixels 0-5 = posições/letras A-F => 1-6, com as cores dos LEDs
 * físicos do D1 Mini + magenta para F. Pixel 6 = conexão (âmbar piscando =
 * (re)conectando; pulso violeta = ocioso). Pixel 7 = processamento (ciano
 * piscando = solving).
 *
 * Changelog:
 *   3.4 - Replay pós-reconexão (M5 do plano de migração, sem o Authorization
 *         — cortado por decisão: tudo roda na rede interna). O id: de cada
 *         evento é rastreado e reenviado como Last-Event-ID no GET seguinte:
 *         se o servidor suportar replay, os eventos emitidos durante a janela
 *         de reconexão (1-30 s de backoff) deixam de ser perdidos — antes,
 *         id:/retry: eram ignorados e um answer emitido com o stream caído
 *         nunca chegava à barra. O retry: do servidor passa a ser honrado no
 *         1º degrau do backoff (falhas consecutivas seguem a tabela), com
 *         faixa sã de 250 ms a 60 s.
 *   3.3 - Split dual-core (M4 do plano de migração). A rede (WiFi + sse.loop
 *         + parse do JSON) sai da loopTask e vai para a netTask, pinada no
 *         core 0 — os pontos que bloqueiam (connect() TCP de até ~5 s,
 *         wifiManager.connect() de até 15 s no boot, Serial dos headers)
 *         deixam de congelar a animação. A loopTask (core 1) vira a uiTask:
 *         drena uma fila de LedCommand e roda leds.update() num tick de 5 ms.
 *         Invariantes: a fila carrega comandos POR VALOR (nunca ponteiro para
 *         o _dataBuf, que é reescrito no próximo evento) e o LedController é
 *         tocado exclusivamente pela loopTask. A saúde do stream é enfileirada
 *         só na transição. Bônus: a animação de "conectando" agora roda desde
 *         o boot (antes o wifiManager.connect() bloqueante segurava o setup).
 *   3.2 - LEDs 6+2 (M3 do plano de migração). As respostas ganham a 6ª
 *         posição (F = magenta) e os pixels 6-7 viram um canal de status
 *         dedicado: conexão no 6, processamento no 7. A escada de prioridades
 *         simplifica — um solving novo NÃO apaga mais a resposta em exibição
 *         (antes vencia a resposta segurada, porque dividiam os mesmos LEDs),
 *         e o stopProcessing() deixou de precisar ser condicional (a cicatriz
 *         da v2.4 se dissolve: status e resposta não disputam mais pixels).
 *         O aborto do processando na queda do stream continua (sem TTL, o
 *         status inicial do servidor ressincroniza ao reabrir). O blackout
 *         pós-boot passa a suprimir a barra inteira (respostas + status).
 *   3.1 - Destravar memória (M2 do plano de migração). Buffers do parser SSE
 *         4 K -> 16 K: no ESP8266 um evento maior que 4 K era descartado em
 *         silêncio (o teto existia por falta de RAM). JSON_DOC_SIZE 512 ->
 *         4096 e REMOÇÃO do filtro do ArduinoJson: o filtro só existia para
 *         ignorar o explanation (campo mais longo) e caber em 512 bytes; sem
 *         ele o documento vê o payload inteiro. O parse segue zero-copy
 *         (contrato char* intacto — strings Linked, pool carrega só a
 *         estrutura); o uso do pool agora é logado a cada answer ([JSON]
 *         pool=) para dar visibilidade antes de um eventual NoMemory.
 *   3.0 - Porte para ESP32-S3 Super Mini (M1 do plano de migração),
 *         comportamento idêntico à v2.5 no D1 Mini. Muda só a borda de
 *         hardware: 5 LEDs discretos -> barra WS2812 (FastLED/RMT, GPIO 13),
 *         ESP8266WiFi -> WiFi.h com WiFi.setSleep(false) (modem sleep do
 *         ESP32 injetaria jitter no stream), yield() -> delay(1) (sob
 *         FreeRTOS o yield() não alimenta o WDT nem cede o tick). Buffers,
 *         filtro JSON e contrato zero-copy (char*) intactos.
 *   2.5 - Robustez do SSE e do answer (validação em hardware pendente):
 *         teto próprio p/ a fase de headers (HEADERS_TIMEOUT_MS) — um half-open
 *         (servidor aceita o TCP e nunca responde) prendia o firmware em
 *         ST_HEADERS para sempre; parse numérico do status HTTP (a
 *         reason-phrase é opcional na RFC 7230 e um "HTTP/1.1 200" seco era
 *         rejeitado); validação de faixa dos slots antes do estreitamento a
 *         uint8_t (257 virava posição 1); log de "cached" (resposta do cache do
 *         servidor) e de "reason" (causa do status error); log do uso do
 *         filtro JSON no boot. Último release com alvo ESP8266/D1 Mini.
 *   2.4 - Evento "status" (LED de processamento). O servidor difunde
 *         status {"state":"solving"} antes de chamar a IA, answer ao terminar e
 *         status {"state":"idle"} em seguida (ou status {"state":"error"} se o
 *         processamento falhar, sem answer). Antes o firmware só reconhecia
 *         event: answer e descartava o status em silêncio — o andamento não
 *         aparecia em lugar nenhum. Agora: solving => LED C (do meio) piscando a
 *         250 ms até chegar answer/error/idle; error => padrão de erro; idle =>
 *         encerra o processando, mas NÃO apaga uma resposta em exibição (no fluxo
 *         normal o idle chega logo após o answer). O solving vence resposta
 *         segurada e encerra o blackout pós-boot. Se o stream cair no meio do
 *         processamento o padrão é abortado, para não esconder a queda — ao
 *         reabrir, o status inicial do servidor ressincroniza o estado.
 *   2.3 - Transporte: o corpo do SSE vem em Transfer-Encoding: chunked (Kestrel)
 *         e o parser não desmontava os frames. O chunk-size aparecia como linha
 *         solta (ignorada por sorte) e o CRLF terminador de cada chunk virava uma
 *         linha vazia espúria: quando um chunk terminava NO MEIO de uma linha
 *         "data:", a linha era cortada ali -> JSON truncado -> showError(). Só
 *         funcionava porque o Kestrel costuma emitir 1 chunk por evento. Agora
 *         SseClient::feedChunkedByte() consome size/CRLF e entrega ao montador de
 *         linhas apenas o corpo lógico. Além disso, TODO header de resposta passa
 *         a ser logado ("[SSE] < ..."): antes só a linha de status ia para a
 *         serial, e foi isso que manteve o problema de framing invisível.
 *   2.2 - Janela de boot: ao ligar, os LEDs sinalizam conexão/ocioso por
 *         LED_BOOT_BLINK_MS (5 min) e então ficam APAGADOS até a 1ª resposta do
 *         backend. O stream segue ativo (ping + logs na serial) durante o
 *         blackout — só a saída dos LEDs é suprimida. A 1ª resposta (qualquer
 *         evento answer: single/multiple/yesno/sequência/test/erro) encerra o
 *         blackout em definitivo e o fluxo normal reassume.
 *   2.1 - Correção: eventos com answerText longo (ex.: matching/ordering com
 *         setas/acentos UTF-8) não acionavam os LEDs. O parse do JSON recebia o
 *         buffer como const char*, fazendo o ArduinoJson COPIAR as strings para
 *         o pool e estourar o StaticJsonDocument (NoMemory) -> handleAnswer dava
 *         return silencioso. Agora o payload é passado como char* (buffer
 *         mutável), ativando o modo zero-copy do ArduinoJson (uso do pool caiu
 *         de ~507/512 para ~160/512). Erro de parse agora sinaliza leds.showError()
 *         em vez de falhar em silêncio. Buffers SSE ampliados 2048 -> 4096.
 *   2.0 - Polling HTTPS substituído por stream SSE persistente (WiFiClient).
 *         Máquina de estados de LED cobrindo todas as situações do backend.
 *         Reconexão automática com backoff. Removido HTTPClient/WiFiClientSecure.
 *   1.0 - Versão inicial por polling.
 *
 * Autor: Proera
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "WiFiManager.h"
#include "LedController.h"
#include "SseClient.h"

// ========================================
// OBJETOS GLOBAIS
// ========================================
WiFiManager wifiManager;
LedController leds;
SseClient sse;

// Documento JSON reutilizado — global para não pesar na pilha da netTask
// (única task que parseia). Sem filtro desde o M2: com 4096 bytes de pool e
// parse zero-copy, o payload inteiro cabe sem descartar nada.
StaticJsonDocument<JSON_DOC_SIZE> g_doc;

unsigned long g_lastHeapLog = 0;

// ========================================
// Split dual-core (M4): netTask (core 0) -> fila -> loop()/uiTask (core 1)
// ========================================
// A netTask roda WiFi + sse.loop() + parse do JSON e PODE bloquear (o
// connect() TCP segura até ~5 s; o wifiManager.connect() do boot, até 15 s).
// A loopTask (core 1) drena a fila e roda leds.update() num tick curto — a
// animação nunca engasga.
//
// INVARIANTES (é aqui que nascem corridas se alguém relaxar):
//   1. A fila carrega LedCommand POR VALOR, com os dados já decodificados.
//      NUNCA enfileirar um ponteiro para o payload (_dataBuf do SseClient):
//      o buffer é reescrito no próximo evento — use-after-write clássico.
//   2. O LedController é tocado EXCLUSIVAMENTE pela loopTask (applyLedCommand
//      + leds.update()). Nenhum handler chama leds.* diretamente.
enum LedCmdType : uint8_t {
  LED_CMD_SINGLE,      // data[0] = posição 1..6
  LED_CMD_MULTIPLE,    // data[0..n-1] = posições
  LED_CMD_YESNO,       // data[0..n-1] = flags (0/1)
  LED_CMD_SLOTS,       // data[0..n-1] = slots (0 = inválido)
  LED_CMD_TEST,        // chase
  LED_CMD_ERROR,       // padrão de erro
  LED_CMD_PROC_ON,     // status solving
  LED_CMD_PROC_OFF,    // status idle / desconhecido
  LED_CMD_CONNECTED,   // value = saúde do stream
};

struct LedCommand {
  uint8_t type;             // LedCmdType
  uint8_t n;                // itens válidos em data[]
  uint8_t data[LED_COUNT];  // posições / slots / flags, por valor
  bool value;               // LED_CMD_CONNECTED
};

static QueueHandle_t g_ledQueue = nullptr;

// Envia sem bloquear: comandos são só exibição, e uma fila cheia (16 posições
// com consumidor a 5 ms) indicaria a uiTask travada — descartar + logar é
// melhor do que prender a netTask.
static void sendLedCommand(const LedCommand& cmd) {
  if (xQueueSend(g_ledQueue, &cmd, 0) != pdTRUE) {
    Serial.println(F("[LED] fila cheia — comando descartado"));
  }
}

static void sendSimpleLedCommand(uint8_t type) {
  LedCommand cmd = {};
  cmd.type = type;
  sendLedCommand(cmd);
}

// Consumidor (roda SÓ na loopTask): traduz o comando em chamadas ao LedController.
static void applyLedCommand(const LedCommand& cmd) {
  switch (cmd.type) {
    case LED_CMD_SINGLE:   leds.showSingle(cmd.data[0]);        break;
    case LED_CMD_MULTIPLE: leds.showMultiple(cmd.data, cmd.n);  break;
    case LED_CMD_YESNO: {
      bool flags[LED_COUNT];
      for (uint8_t i = 0; i < cmd.n && i < LED_COUNT; i++) {
        flags[i] = cmd.data[i] != 0;
      }
      leds.showYesNo(flags, cmd.n);
      break;
    }
    case LED_CMD_SLOTS:     leds.showSlots(cmd.data, cmd.n);    break;
    case LED_CMD_TEST:      leds.showTestChase();               break;
    case LED_CMD_ERROR:     leds.showError();                   break;
    case LED_CMD_PROC_ON:   leds.showProcessing();              break;
    case LED_CMD_PROC_OFF:  leds.stopProcessing();              break;
    case LED_CMD_CONNECTED: leds.setConnected(cmd.value);       break;
  }
}

// ========================================
// Decisão de LEDs a partir do payload do evento "answer"
// ========================================
// payload é o buffer mutável do SseClient: o ArduinoJson parseia em zero-copy
// (chaves/strings apontam para o buffer, sem cópia no pool). As strings extraídas
// (ex.: answerText) são válidas durante toda esta função, pois o buffer só é
// reescrito no próximo evento. Roda na netTask: daqui só saem LedCommand por
// valor — nada de tocar no LedController nem de enfileirar ponteiros.
void handleAnswer(char* payload) {
  g_doc.clear();
  DeserializationError err = deserializeJson(g_doc, payload);

  if (err) {
    // Em vez de falhar em silêncio (que tornava esse tipo de bug invisível),
    // sinaliza nos próprios LEDs que um evento chegou mas não pôde ser exibido.
    Serial.print(F("[JSON] Erro ao parsear: "));
    Serial.println(err.c_str());
    sendSimpleLedCommand(LED_CMD_ERROR);
    return;
  }

  bool hasData = g_doc["hasData"] | false;
  const char* qt = g_doc["questionType"] | "";
  const char* answerText = g_doc["answerText"] | "";

  // Uso do pool: com zero-copy ele carrega só a estrutura, mas é o indicador
  // de quando JSON_DOC_SIZE precisar crescer (NoMemory viraria showError()).
  Serial.print(F("[JSON] pool="));
  Serial.print(g_doc.memoryUsage());
  Serial.print(F("/"));
  Serial.println(JSON_DOC_SIZE);

  Serial.print(F("[ANSWER] questionType="));
  Serial.print(qt);
  Serial.print(F(" hasData="));
  Serial.print(hasData ? F("true") : F("false"));
  Serial.print(F(" answerText=\""));
  Serial.print(answerText);
  Serial.print(F("\""));
  if (g_doc["cached"] | false) {
    Serial.print(F(" (cache do servidor)"));
  }
  Serial.println();

  // 1) test => varredura de conectividade (não é resposta real).
  if (strcmp(qt, "test") == 0) {
    Serial.println(F("[ANSWER] evento de teste -> chase A->F"));
    sendSimpleLedCommand(LED_CMD_TEST);
    return;
  }

  // 2) ilegível => padrão de erro.
  if (!hasData) {
    Serial.println(F("[ANSWER] questão ilegível -> erro (pixels de resposta piscando)"));
    sendSimpleLedCommand(LED_CMD_ERROR);
    return;
  }

  // 3) single / multiple => letras.
  if (strcmp(qt, "single") == 0 || strcmp(qt, "multiple") == 0) {
    JsonArray letters = g_doc["letters"];
    LedCommand cmd = {};
    for (JsonVariant v : letters) {
      const char* s = v.as<const char*>();
      if (s && s[0] && !s[1]) {  // exatamente 1 caractere
        char c = toupper((unsigned char)s[0]);
        if (c >= 'A' && c < 'A' + LED_COUNT) {  // A..F (6 posições desde o M3)
          cmd.data[cmd.n++] = (uint8_t)(c - 'A' + 1);
          if (cmd.n >= LED_COUNT) break;
        }
      }
    }
    if (cmd.n == 0) {
      Serial.println(F("[ANSWER] letras vazias/inválidas -> erro"));
      sendSimpleLedCommand(LED_CMD_ERROR);
      return;
    }
    cmd.type = (strcmp(qt, "single") == 0) ? LED_CMD_SINGLE : LED_CMD_MULTIPLE;
    sendLedCommand(cmd);
    return;
  }

  // 4a) yesno => flags (retido c/ TTL: Sim = fixo, Não = piscando, simultâneos).
  if (strcmp(qt, "yesno") == 0) {
    int slotCount = g_doc["slotCount"] | 0;
    JsonArray flagsArr = g_doc["flags"];
    LedCommand cmd = {};
    cmd.type = LED_CMD_YESNO;
    for (JsonVariant v : flagsArr) {
      if (cmd.n >= LED_COUNT) break;
      cmd.data[cmd.n++] = v.as<bool>() ? 1 : 0;
    }
    if (slotCount > LED_COUNT) {
      Serial.print(F("[ANSWER] yesno com slotCount="));
      Serial.print(slotCount);
      Serial.print(F(" — exibindo só as "));
      Serial.print(LED_COUNT);
      Serial.println(F(" primeiras (truncado)"));
    }
    sendLedCommand(cmd);
    return;
  }

  // 4b) dropdown / ordering / matching => slots (sequencial).
  if (strcmp(qt, "dropdown") == 0 || strcmp(qt, "ordering") == 0 ||
      strcmp(qt, "matching") == 0) {
    int slotCount = g_doc["slotCount"] | 0;
    JsonArray slotsArr = g_doc["slots"];
    LedCommand cmd = {};
    cmd.type = LED_CMD_SLOTS;
    for (JsonVariant v : slotsArr) {
      if (cmd.n >= LED_COUNT) break;
      // Validar ANTES de estreitar: (uint8_t)257 vira 1 e seria exibido como a
      // posição 1, com toda a confiança, em vez de sinalizar dado inválido.
      // Fora da faixa vira 0, que showSlots() já trata como slot inválido.
      const int raw = v.as<int>();
      const bool inRange = (raw >= 0 && raw <= 255);
      if (!inRange) {
        Serial.print(F("[ANSWER] slot fora da faixa: "));
        Serial.println(raw);
      }
      cmd.data[cmd.n++] = inRange ? (uint8_t)raw : 0;
    }
    if (slotCount > LED_COUNT) {
      Serial.print(F("[ANSWER] slots com slotCount="));
      Serial.print(slotCount);
      Serial.print(F(" — exibindo só os "));
      Serial.print(LED_COUNT);
      Serial.println(F(" primeiros (truncado)"));
    }
    sendLedCommand(cmd);
    return;
  }

  // questionType desconhecido => trata como erro.
  Serial.print(F("[ANSWER] questionType desconhecido: "));
  Serial.println(qt);
  sendSimpleLedCommand(LED_CMD_ERROR);
}

// ========================================
// Decisão de LEDs a partir do payload do evento "status"
// ========================================
// Sequência normal de um solve: solving -> answer -> idle. Em falha:
// solving -> error (sem answer). O idle que chega logo após um answer NÃO deve
// apagar a resposta — quem garante isso é LedController::stopProcessing(), que
// só age se o que está no ar é o próprio "processando".
void handleStatus(char* payload) {
  g_doc.clear();
  DeserializationError err = deserializeJson(g_doc, payload);

  if (err) {
    // Diferente do answer, um status corrompido NÃO aciona leds.showError():
    // status é informativo e o padrão de erro é reservado às respostas (piscar
    // os 5 aqui faria o usuário ler "questão ilegível"). Fica registrado na serial.
    Serial.print(F("[JSON] Erro ao parsear status: "));
    Serial.println(err.c_str());
    return;
  }

  const char* state = g_doc["state"] | "";
  int activeSolves = g_doc["activeSolves"] | 0;

  Serial.print(F("[STATUS] state="));
  Serial.print(state);
  Serial.print(F(" activeSolves="));
  Serial.println(activeSolves);

  if (strcmp(state, "solving") == 0) {
    // Canal próprio (pixel 7): sinaliza sem apagar a resposta em exibição.
    sendSimpleLedCommand(LED_CMD_PROC_ON);
    return;
  }

  if (strcmp(state, "error") == 0) {
    // O backend manda a causa em "reason" (timeout, invalid_output,
    // upstream_error, internal_error). Sem isto o erro só piscava os 5 LEDs e a
    // serial não dizia por quê.
    const char* reason = g_doc["reason"] | "";
    Serial.print(F("[STATUS] falha no processamento"));
    if (reason[0]) {
      Serial.print(F(" (reason="));
      Serial.print(reason);
      Serial.print(F(")"));
    }
    Serial.println(F(" -> erro (pixels de resposta piscando)"));
    sendSimpleLedCommand(LED_CMD_ERROR);
    return;
  }

  // idle — e qualquer state desconhecido, que a spec manda tratar como idle.
  sendSimpleLedCommand(LED_CMD_PROC_OFF);
}

// ========================================
// netTask (core 0): WiFi + stream + parse — pode bloquear
// ========================================
void netTask(void*) {
  // Conexão WiFi inicial (bloqueia até 15 s — só esta task; a animação de
  // "conectando" já roda na loopTask desde o boot).
  wifiManager.connect();
  WiFi.setAutoReconnect(true);

  sse.begin(handleAnswer, handleStatus);
  Serial.println(F("✓ netTask ativa. Aguardando stream...\n"));

  // Saúde do stream: enfileirada SÓ na transição (o estado inicial false já é
  // o default do LedController), para não inundar a fila a cada iteração.
  bool lastStreaming = false;

  for (;;) {
    sse.loop();

    bool streaming = sse.isStreaming();
    if (streaming != lastStreaming) {
      lastStreaming = streaming;
      LedCommand cmd = {};
      cmd.type = LED_CMD_CONNECTED;
      cmd.value = streaming;
      sendLedCommand(cmd);
    }

    delay(1);  // tick do FreeRTOS (alimenta o WDT; yield() não faria isso)
  }
}

// ========================================
// SETUP
// ========================================
void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(100);

  Serial.println(F("\n\n"));
  Serial.println(F("╔═══════════════════════════════════════════════╗"));
  Serial.println(F("║  CertMind - Cliente de Stream ESP32-S3        ║"));
  Serial.println(F("║  Versão: 3.4 (SSE)                            ║"));
  Serial.println(F("╚═══════════════════════════════════════════════╝"));

  leds.begin();

  g_ledQueue = xQueueCreate(16, sizeof(LedCommand));

  // Rede no core 0 (junto do driver WiFi); a loopTask do Arduino roda no
  // core 1 e fica exclusiva da animação.
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);

  Serial.println(F("✓ Setup concluído (uiTask no core 1, netTask no core 0)\n"));
}

// ========================================
// LOOP = uiTask (core 1): fila -> LedController, nunca bloqueia
// ========================================
void loop() {
  unsigned long now = millis();

  // 1) Aplica os comandos pendentes (por valor, vindos da netTask).
  LedCommand cmd;
  while (xQueueReceive(g_ledQueue, &cmd, 0) == pdTRUE) {
    applyLedCommand(cmd);
  }

  // 2) Avança a animação dos LEDs.
  leds.update();

  // 3) Log periódico do heap (detecção de vazamento).
  if (now - g_lastHeapLog > STREAM_HEAP_LOG_MS) {
    g_lastHeapLog = now;
    Serial.print(F("[HEAP] livre="));
    Serial.println(ESP.getFreeHeap());
  }

  delay(5);  // tick da UI: 5 ms é folga p/ o blink mais rápido (150 ms)
}
