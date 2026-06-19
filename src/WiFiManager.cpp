/*
 * WiFiManager.cpp
 * 
 * Implementação do módulo de Gerenciamento WiFi
 */

#include "WiFiManager.h"

WiFiManager::WiFiManager() {
  // Construtor vazio - usa configurações do Config.h
}

bool WiFiManager::connect() {
  Serial.println("\n=================================");
  Serial.print("Conectando ao WiFi: ");
  Serial.println(WIFI_SSID);
  Serial.println("=================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_MAX_RETRY_ATTEMPTS) {
    delay(WIFI_RETRY_DELAY_MS);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✓ WiFi conectado com sucesso!");
    Serial.print("Endereço IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("=================================\n");
    return true;
  } else {
    Serial.println("✗ Falha ao conectar ao WiFi!");
    Serial.println("Verifique as credenciais e tente novamente.");
    Serial.println("=================================\n");
    return false;
  }
}

bool WiFiManager::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String WiFiManager::getIP() {
  if (isConnected()) {
    return WiFi.localIP().toString();
  }
  return "0.0.0.0";
}

bool WiFiManager::reconnect() {
  if (isConnected()) {
    return true;
  }
  
  Serial.println("⚠ WiFi desconectado! Tentando reconectar...");
  return connect();
}
