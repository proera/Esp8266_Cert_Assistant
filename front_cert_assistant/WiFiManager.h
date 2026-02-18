/*
 * WiFiManager.h
 * 
 * Módulo de Gerenciamento WiFi
 * Responsabilidade: Gerenciar conexão WiFi e reconexão
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <ESP8266WiFi.h>
#include "Config.h"

class WiFiManager {
  public:
    // Construtor
    WiFiManager();
    
    // Conecta ao WiFi
    bool connect();
    
    // Verifica se está conectado
    bool isConnected();
    
    // Obtém o endereço IP
    String getIP();
    
    // Tenta reconectar
    bool reconnect();
};

#endif // WIFI_MANAGER_H
