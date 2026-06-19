/*
 * Sistema CertMind - Cliente de Stream ESP8266 (D1 Mini)
 *
 * Versão: 2.2
 *
 * Descrição: Consome o stream SSE da API CertMind por UMA conexão HTTP
 * persistente (GET, texto claro, sem TLS) e aciona 5 LEDs (A-E) conforme
 * cada situação emitida pelo backend (test, ilegível, single, multiple,
 * yesno, dropdown, ordering, matching) e a saúde da conexão.
 *
 * O ESP8266 é apenas consumidor: abre o GET e fica escutando. Não faz POST,
 * não envia imagem, não faz request/response.
 *
 * Hardware: D1 Mini (ESP8266) + 5 LEDs (posições/letras A-E => 1-5).
 *
 * Changelog:
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
#include <ESP8266WiFi.h>
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

// Documento JSON reutilizado (com filtro) + filtro — globais para não estourar
// a pilha do loop() do ESP8266.
StaticJsonDocument<JSON_FILTER_SIZE> g_filter;
StaticJsonDocument<JSON_DOC_SIZE> g_doc;

unsigned long g_lastHeapLog = 0;

// ========================================
// Decisão de LEDs a partir do payload do evento "answer"
// ========================================
// payload é o buffer mutável do SseClient: o ArduinoJson parseia em zero-copy
// (chaves/strings apontam para o buffer, sem cópia no pool). As strings extraídas
// (ex.: answerText) são válidas durante toda esta função, pois o buffer só é
// reescrito no próximo evento.
void handleAnswer(char* payload) {
  g_doc.clear();
  DeserializationError err =
      deserializeJson(g_doc, payload, DeserializationOption::Filter(g_filter));

  if (err) {
    // Em vez de falhar em silêncio (que tornava esse tipo de bug invisível),
    // sinaliza nos próprios LEDs que um evento chegou mas não pôde ser exibido.
    Serial.print(F("[JSON] Erro ao parsear: "));
    Serial.println(err.c_str());
    leds.showError();
    return;
  }

  bool hasData = g_doc["hasData"] | false;
  const char* qt = g_doc["questionType"] | "";
  const char* answerText = g_doc["answerText"] | "";

  Serial.print(F("[ANSWER] questionType="));
  Serial.print(qt);
  Serial.print(F(" hasData="));
  Serial.print(hasData ? F("true") : F("false"));
  Serial.print(F(" answerText=\""));
  Serial.print(answerText);
  Serial.println(F("\""));

  // 1) test => varredura de conectividade (não é resposta real).
  if (strcmp(qt, "test") == 0) {
    Serial.println(F("[ANSWER] evento de teste -> chase A->E"));
    leds.showTestChase();
    return;
  }

  // 2) ilegível => padrão de erro.
  if (!hasData) {
    Serial.println(F("[ANSWER] questão ilegível -> erro (5 LEDs piscando)"));
    leds.showError();
    return;
  }

  // 3) single / multiple => letras.
  if (strcmp(qt, "single") == 0 || strcmp(qt, "multiple") == 0) {
    JsonArray letters = g_doc["letters"];
    uint8_t pos[LED_COUNT];
    uint8_t n = 0;
    for (JsonVariant v : letters) {
      const char* s = v.as<const char*>();
      if (s && s[0] && !s[1]) {  // exatamente 1 caractere
        char c = toupper((unsigned char)s[0]);
        if (c >= 'A' && c <= 'E') {
          pos[n++] = (uint8_t)(c - 'A' + 1);
          if (n >= LED_COUNT) break;
        }
      }
    }
    if (n == 0) {
      Serial.println(F("[ANSWER] letras vazias/inválidas -> erro"));
      leds.showError();
      return;
    }
    if (strcmp(qt, "single") == 0) {
      leds.showSingle(pos[0]);
    } else {
      leds.showMultiple(pos, n);
    }
    return;
  }

  // 4a) yesno => flags (retido c/ TTL: Sim = fixo, Não = piscando, simultâneos).
  if (strcmp(qt, "yesno") == 0) {
    int slotCount = g_doc["slotCount"] | 0;
    JsonArray flagsArr = g_doc["flags"];
    bool flags[LED_COUNT];
    uint8_t n = 0;
    for (JsonVariant v : flagsArr) {
      if (n >= LED_COUNT) break;
      flags[n++] = v.as<bool>();
    }
    if (slotCount > LED_COUNT) {
      Serial.print(F("[ANSWER] yesno com slotCount="));
      Serial.print(slotCount);
      Serial.println(F(" — exibindo só as 5 primeiras (truncado)"));
    }
    leds.showYesNo(flags, n);
    return;
  }

  // 4b) dropdown / ordering / matching => slots (sequencial).
  if (strcmp(qt, "dropdown") == 0 || strcmp(qt, "ordering") == 0 ||
      strcmp(qt, "matching") == 0) {
    int slotCount = g_doc["slotCount"] | 0;
    JsonArray slotsArr = g_doc["slots"];
    uint8_t slots[LED_COUNT];
    uint8_t n = 0;
    for (JsonVariant v : slotsArr) {
      if (n >= LED_COUNT) break;
      slots[n++] = (uint8_t)(v.as<int>());
    }
    if (slotCount > LED_COUNT) {
      Serial.print(F("[ANSWER] slots com slotCount="));
      Serial.print(slotCount);
      Serial.println(F(" — exibindo só os 5 primeiros (truncado)"));
    }
    leds.showSlots(slots, n);
    return;
  }

  // questionType desconhecido => trata como erro.
  Serial.print(F("[ANSWER] questionType desconhecido: "));
  Serial.println(qt);
  leds.showError();
}

// ========================================
// SETUP
// ========================================
void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(100);

  Serial.println(F("\n\n"));
  Serial.println(F("╔═══════════════════════════════════════════════╗"));
  Serial.println(F("║  CertMind - Cliente de Stream ESP8266         ║"));
  Serial.println(F("║  Versão: 2.2 (SSE)                            ║"));
  Serial.println(F("╚═══════════════════════════════════════════════╝"));

  leds.begin();

  // Filtro do ArduinoJson: parseia só o necessário (ignora explanation, que
  // pode ser longa), mantendo o documento pequeno.
  g_filter["hasData"] = true;
  g_filter["questionType"] = true;
  g_filter["letters"] = true;
  g_filter["flags"] = true;
  g_filter["slots"] = true;
  g_filter["slotCount"] = true;
  g_filter["answerText"] = true;

  // Conexão WiFi inicial (bloqueante apenas no boot); auto-reconnect cuida do resto.
  wifiManager.connect();
  WiFi.setAutoReconnect(true);

  sse.begin(handleAnswer);

  Serial.println(F("✓ Setup concluído. Aguardando stream...\n"));
}

// ========================================
// LOOP (não-bloqueante)
// ========================================
void loop() {
  unsigned long now = millis();

  // 1) Avança a animação dos LEDs.
  leds.update();

  // 2) Gerencia o stream (conexão/parsing/reconexão).
  sse.loop();

  // 3) Reflete a saúde do stream nos LEDs (só afeta quando não há resposta ativa).
  leds.setConnected(sse.isStreaming());

  // 4) Log periódico do heap (detecção de vazamento).
  if (now - g_lastHeapLog > STREAM_HEAP_LOG_MS) {
    g_lastHeapLog = now;
    Serial.print(F("[HEAP] livre="));
    Serial.println(ESP.getFreeHeap());
  }

  yield();  // alimenta o watchdog
}
