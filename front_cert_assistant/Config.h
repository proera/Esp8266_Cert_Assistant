/*
 * Config.h
 * 
 * Configurações Centralizadas do Sistema
 * 
 * Este arquivo contém todas as constantes e configurações do projeto.
 * Edite aqui para alterar credenciais, pins, intervalos, etc.
 */

#ifndef CONFIG_H
#define CONFIG_H

// ========================================
// CONFIGURAÇÕES WiFi
// ========================================
#define WIFI_SSID "Sagaz"
#define WIFI_PASSWORD "Amarelo%4815"
#define WIFI_MAX_RETRY_ATTEMPTS 30
#define WIFI_RETRY_DELAY_MS 500
#define WIFI_RECONNECT_DELAY_MS 5000

// ========================================
// CONFIGURAÇÕES API
// ========================================
#define API_URL "https://certapi.proera.com.br/api/Esp8266/poll"

// ========================================
// CONFIGURAÇÕES DE PINOS DOS LEDs
// ========================================
#define LED_PIN_A D3  // GPIO0  - LED Verde (Resposta A)
#define LED_PIN_B D2  // GPIO4  - LED Amarelo (Resposta B)
#define LED_PIN_C D5  // GPIO14 - LED Vermelho (Resposta C)
#define LED_PIN_D D1  // GPIO5  - LED Azul (Resposta D)
#define LED_PIN_E D7  // GPIO13 - LED Branco (Resposta E)

// ========================================
// CONFIGURAÇÕES DE TIMING
// ========================================
#define POLLING_INTERVAL_MS 2000  // Intervalo entre polls (1.5 segundos)
#define DISPLAY_DURATION_SINGLE_MS 5000   // Tempo de exibição p/ questão única escolha
#define DISPLAY_DURATION_MULTIPLE_MS 8000 // Tempo de exibição p/ questão múltipla escolha
#define LOOP_DELAY_MS 10  // Delay no loop principal

// ========================================
// CONFIGURAÇÕES SERIAL
// ========================================
#define SERIAL_BAUD_RATE 115200

// ========================================
// CONFIGURAÇÕES JSON
// ========================================
#define JSON_BUFFER_SIZE 1024  // Tamanho do buffer para parsing JSON

#endif // CONFIG_H
