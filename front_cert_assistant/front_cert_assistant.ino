/*
 * Sistema de Polling ESP8266 - D1 Mini
 * 
 * Descrição: Sistema que faz polling a cada 2 segundos ao endpoint da API,
 * extrai dados JSON (isMultipleChoice e correctAnswers) e controla LEDs
 * baseado nas respostas corretas recebidas.
 * 
 * Hardware: D1 Mini (ESP8266)
 * 
 * Autor: Proera
 * Data: 2026-01-09
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "WiFiManager.h"

// ========================================
// VARIÁVEIS GLOBAIS
// ========================================
unsigned long lastPollTime = 0;
unsigned long pollCount = 0;

// ========================================
// OBJETOS GLOBAIS
// ========================================
WiFiManager wifiManager;
WiFiClientSecure wifiClient;
HTTPClient http;



// ========================================
// FUNÇÃO: Inicializar LEDs
// ========================================
void initializeLEDs() {
  pinMode(LED_PIN_A, OUTPUT);
  pinMode(LED_PIN_B, OUTPUT);
  pinMode(LED_PIN_C, OUTPUT);
  pinMode(LED_PIN_D, OUTPUT);
  pinMode(LED_PIN_E, OUTPUT);

  // Desliga todos os LEDs inicialmente
  digitalWrite(LED_PIN_A, LOW);
  digitalWrite(LED_PIN_B, LOW);
  digitalWrite(LED_PIN_C, LOW);
  digitalWrite(LED_PIN_D, LOW);
  digitalWrite(LED_PIN_E, LOW);

  Serial.println("✓ LEDs inicializados");
}

// ========================================
// FUNÇÃO: Desligar todos os LEDs
// ========================================
void turnOffAllLEDs() {
  digitalWrite(LED_PIN_A, LOW);
  digitalWrite(LED_PIN_B, LOW);
  digitalWrite(LED_PIN_C, LOW);
  digitalWrite(LED_PIN_D, LOW);
  digitalWrite(LED_PIN_E, LOW);
}

// ========================================
// FUNÇÃO: Atualizar LEDs baseado nas respostas corretas
// ========================================
void updateLEDs(JsonArray correctAnswers, bool isMultipleChoice) {
  // Primeiro, desliga todos os LEDs
  turnOffAllLEDs();

  Serial.println("\nLEDs atualizados:");

  // Percorre o array de respostas corretas
  for (JsonVariant answer : correctAnswers) {
    String answerStr = answer.as<String>();

    if (answerStr == "A") {
      digitalWrite(LED_PIN_A, HIGH);
      Serial.println("  ✓ LED A (Verde): LIGADO");
    } else if (answerStr == "B") {
      digitalWrite(LED_PIN_B, HIGH);
      Serial.println("  ✓ LED B (Amarelo): LIGADO");
    } else if (answerStr == "C") {
      digitalWrite(LED_PIN_C, HIGH);
      Serial.println("  ✓ LED C (Vermelho): LIGADO");
    } else if (answerStr == "D") {
      digitalWrite(LED_PIN_D, HIGH);
      Serial.println("  ✓ LED D (Azul): LIGADO");
    } else if (answerStr == "E") {
      digitalWrite(LED_PIN_E, HIGH);
      Serial.println("  ✓ LED E (Branco): LIGADO");
    }
  }

  // Estrutura de dados para facilitar iteração sobre os LEDs
  struct LED {
    int pin;
    const char* name;
  };
  
  LED leds[] = {
    {LED_PIN_A, "LED A (Verde)"},
    {LED_PIN_B, "LED B (Amarelo)"},
    {LED_PIN_C, "LED C (Vermelho)"},
    {LED_PIN_D, "LED D (Azul)"},
    {LED_PIN_E, "LED E (Branco)"}
  };

  // Mostra os LEDs que ficaram desligados
  for (int i = 0; i < 5; i++) {
    if (digitalRead(leds[i].pin) == LOW) {
      Serial.print("    ");
      Serial.print(leds[i].name);
      Serial.println(": DESLIGADO");
    }
  }

  // Define delay baseado no tipo de questão
  int delayTime = isMultipleChoice ? DISPLAY_DURATION_MULTIPLE_MS : DISPLAY_DURATION_SINGLE_MS;
  delay(delayTime);
}

// ========================================
// FUNÇÃO: Fazer requisição HTTP GET
// ========================================
String makeHTTPRequest() {
  String payload = "";

  // Configura cliente HTTPS (desabilita validação de certificado)
  wifiClient.setInsecure();

  // Inicia a conexão HTTP
  http.begin(wifiClient, API_URL);

  // Adiciona header Accept
  http.addHeader("Accept", "text/plain");

  // Faz a requisição GET
  int httpCode = http.GET();

  // Verifica o código de resposta
  if (httpCode > 0) {
    Serial.print("Status HTTP: ");
    Serial.println(httpCode);

    if (httpCode == HTTP_CODE_OK) {
      payload = http.getString();
      Serial.println("✓ Response recebido com sucesso!");
    } else {
      Serial.print("✗ Erro HTTP: ");
      Serial.println(httpCode);
    }
  } else {
    Serial.print("✗ Falha na requisição. Erro: ");
    Serial.println(http.errorToString(httpCode));
  }

  // Finaliza a conexão
  http.end();

  return payload;
}

// ========================================
// FUNÇÃO: Parsear JSON e extrair dados
// ========================================
void parseAndExtractData(String jsonString) {
  // Cria documento JSON com capacidade adequada
  StaticJsonDocument<JSON_BUFFER_SIZE> doc;

  // Deserializa o JSON
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    Serial.print("✗ Erro ao parsear JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // Verifica se há dados disponíveis
  bool hasData = doc["hasData"] | false;

  if (!hasData) {
    Serial.println("⚠ Sem dados disponíveis (hasData: false)");
    turnOffAllLEDs();
    return;
  }

  Serial.println("\n--- Dados Extraídos ---");

  // Extrai isMultipleChoice
  bool isMultipleChoice = doc["data"]["isMultipleChoice"] | false;
  Serial.print("isMultipleChoice: ");
  Serial.println(isMultipleChoice ? "true" : "false");

  // Extrai correctAnswers (array)
  JsonArray correctAnswers = doc["data"]["correctAnswers"];

  Serial.print("correctAnswers: [");
  for (size_t i = 0; i < correctAnswers.size(); i++) {
    if (i > 0) Serial.print(", ");
    Serial.print(correctAnswers[i].as<String>());
  }
  Serial.println("]");

  Serial.println("-----------------------");

  // Atualiza os LEDs baseado nas respostas corretas e tipo de questão
  updateLEDs(correctAnswers, isMultipleChoice);
}

// ========================================
// FUNÇÃO: Executar Polling
// ========================================
void performPolling() {
  pollCount++;

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.print("║ Poll #");
  Serial.print(pollCount);
  Serial.println(" - Requisição iniciada...");
  Serial.println("╚════════════════════════════════════════╝");

  // Faz a requisição HTTP
  String response = makeHTTPRequest();

  // Se recebeu resposta, parseia o JSON
  if (response.length() > 0) {
    parseAndExtractData(response);
  } else {
    Serial.println("✗ Resposta vazia ou erro na requisição");
  }

  Serial.println("\n⏳ Aguardando próximo poll (2 segundos)...\n");
}

// ========================================
// SETUP: Configuração inicial
// ========================================
void setup() {
  // Inicializa comunicação Serial
  Serial.begin(SERIAL_BAUD_RATE);
  delay(100);

  Serial.println("\n\n");
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║  Sistema de Polling ESP8266 - D1 Mini        ║");
  Serial.println("║  Versão: 1.0                                  ║");
  Serial.println("╚═══════════════════════════════════════════════╝");

  // Inicializa LEDs
  initializeLEDs();

  // Conecta ao WiFi
  wifiManager.connect();

  // Verifica se conectou ao WiFi
  if (!wifiManager.isConnected()) {
    Serial.println("⚠ Sistema iniciado sem conexão WiFi!");
    Serial.println("Tentando reconectar...\n");
  }

  Serial.println("✓ Setup concluído!\n");
  Serial.println("Iniciando polling...\n");
}

// ========================================
// LOOP: Loop principal (não-bloqueante)
// ========================================
void loop() {
  // Verifica se está conectado ao WiFi
  if (!wifiManager.isConnected()) {
    wifiManager.reconnect();
    delay(WIFI_RECONNECT_DELAY_MS);
    return;
  }

  // Verifica se passou o intervalo de polling (sistema não-bloqueante)
  unsigned long currentTime = millis();

  if (currentTime - lastPollTime >= POLLING_INTERVAL_MS) {
    lastPollTime = currentTime;
    performPolling();
  }

  // Pequeno delay para não sobrecarregar o processador
  delay(LOOP_DELAY_MS);
}
