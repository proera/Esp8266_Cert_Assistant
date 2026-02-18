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

// ========================================
// CONFIGURAÇÕES WiFi (HARDCODED)
// ========================================
const char* ssid = "Sagaz";             // Alterar para o nome da sua rede WiFi
const char* password = "Amarelo%4815";  // Alterar para a senha da sua rede WiFi

// ========================================
// CONFIGURAÇÕES API
// ========================================
const char* apiUrl = "https://certapi.proera.com.br/api/Esp8266/poll";

// ========================================
// CONFIGURAÇÕES DE PINOS DOS LEDs
// ========================================
#define LED_A D3  // GPIO0  - LED Verde (Resposta A)
#define LED_B D2  // GPIO4  - LED Amarelo (Resposta B)
#define LED_C D5  // GPIO14 - LED Vermelho (Resposta C)
#define LED_D D1  // GPIO5  - LED Azul (Resposta D)
#define LED_E D7  // GPIO13 - LED Branco (Resposta E)

// ========================================
// CONFIGURAÇÕES DE TIMING
// ========================================
const unsigned long POLLING_INTERVAL = 1500;  // 2 segundos em milissegundos
unsigned long lastPollTime = 0;
unsigned long pollCount = 0;

// ========================================
// OBJETOS GLOBAIS
// ========================================
WiFiClientSecure wifiClient;
HTTPClient http;

// ========================================
// FUNÇÃO: Conectar ao WiFi
// ========================================
void connectToWiFi() {
  Serial.println("\n=================================");
  Serial.print("Conectando ao WiFi: ");
  Serial.println(ssid);
  Serial.println("=================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi conectado com sucesso!");
    Serial.print("Endereço IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("=================================\n");
  } else {
    Serial.println("\n✗ Falha ao conectar ao WiFi!");
    Serial.println("Verifique as credenciais e tente novamente.");
    Serial.println("=================================\n");
  }
}

// ========================================
// FUNÇÃO: Inicializar LEDs
// ========================================
void initializeLEDs() {
  pinMode(LED_A, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(LED_C, OUTPUT);
  pinMode(LED_D, OUTPUT);
  pinMode(LED_E, OUTPUT);

  // Desliga todos os LEDs inicialmente
  digitalWrite(LED_A, LOW);
  digitalWrite(LED_B, LOW);
  digitalWrite(LED_C, LOW);
  digitalWrite(LED_D, LOW);
  digitalWrite(LED_E, LOW);

  Serial.println("✓ LEDs inicializados");
}

// ========================================
// FUNÇÃO: Desligar todos os LEDs
// ========================================
void turnOffAllLEDs() {
  digitalWrite(LED_A, LOW);
  digitalWrite(LED_B, LOW);
  digitalWrite(LED_C, LOW);
  digitalWrite(LED_D, LOW);
  digitalWrite(LED_E, LOW);
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
      digitalWrite(LED_A, HIGH);
      Serial.println("  ✓ LED A (Verde): LIGADO");
    } else if (answerStr == "B") {
      digitalWrite(LED_B, HIGH);
      Serial.println("  ✓ LED B (Amarelo): LIGADO");
    } else if (answerStr == "C") {
      digitalWrite(LED_C, HIGH);
      Serial.println("  ✓ LED C (Vermelho): LIGADO");
    } else if (answerStr == "D") {
      digitalWrite(LED_D, HIGH);
      Serial.println("  ✓ LED D (Azul): LIGADO");
    } else if (answerStr == "E") {
      digitalWrite(LED_E, HIGH);
      Serial.println("  ✓ LED E (Branco): LIGADO");
    }
  }

  // Estrutura de dados para facilitar iteração sobre os LEDs
  struct LED {
    int pin;
    const char* name;
  };
  
  LED leds[] = {
    {LED_A, "LED A (Verde)"},
    {LED_B, "LED B (Amarelo)"},
    {LED_C, "LED C (Vermelho)"},
    {LED_D, "LED D (Azul)"},
    {LED_E, "LED E (Branco)"}
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
  int delayTime = isMultipleChoice ? 8000 : 5000;
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
  http.begin(wifiClient, apiUrl);

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
  StaticJsonDocument<1024> doc;

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
  Serial.begin(115200);
  delay(100);

  Serial.println("\n\n");
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║  Sistema de Polling ESP8266 - D1 Mini        ║");
  Serial.println("║  Versão: 1.0                                  ║");
  Serial.println("╚═══════════════════════════════════════════════╝");

  // Inicializa LEDs
  initializeLEDs();

  // Conecta ao WiFi
  connectToWiFi();

  // Verifica se conectou ao WiFi
  if (WiFi.status() != WL_CONNECTED) {
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
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ WiFi desconectado! Tentando reconectar...");
    connectToWiFi();
    delay(5000);
    return;
  }

  // Verifica se passou o intervalo de polling (sistema não-bloqueante)
  unsigned long currentTime = millis();

  if (currentTime - lastPollTime >= POLLING_INTERVAL) {
    lastPollTime = currentTime;
    performPolling();
  }

  // Pequeno delay para não sobrecarregar o processador
  delay(10);
}
