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

#include <Arduino.h>

// ========================================
// CONFIGURAÇÕES WiFi
// ========================================
#define WIFI_SSID "Sagaz"
#define WIFI_PASSWORD "Amarelo%4815"
#define WIFI_MAX_RETRY_ATTEMPTS 30
#define WIFI_RETRY_DELAY_MS 500

// ========================================
// CONFIGURAÇÕES DO STREAM (SSE)
// ========================================
// Endpoint de consumo (GET persistente, HTTP puro — sem TLS).
// Em dev local, basta trocar host/porta aqui (ex.: 192.168.15.38:5267).
#define STREAM_HOST "192.168.15.38"
#define STREAM_PORT 8090
#define STREAM_PATH "/api/exam/stream"

// Reconexão / saúde do stream
#define STREAM_TIMEOUT_MS 40000UL   // sem nenhuma linha por > ~40s (>2x ping de 15s) => reconecta
#define STREAM_HEAP_LOG_MS 10000UL  // intervalo de log do heap livre

// Backoff progressivo de reconexão (ms): 1s -> 2s -> 5s -> 10s -> 20s -> 30s (máx)
#define STREAM_BACKOFF_TABLE { 1000UL, 2000UL, 5000UL, 10000UL, 20000UL, 30000UL }

// Tetos de buffer do parser SSE (descarta/reseta se exceder).
// Folga p/ payloads verbosos (answerText + explanation longos em UTF-8).
#define SSE_MAX_LINE 4096   // teto por linha recebida
#define SSE_MAX_DATA 4096   // teto do payload acumulado (campos data:)

// ========================================
// CONFIGURAÇÕES DE PINOS DOS LEDs
// ========================================
// 5 LEDs no total, mapeados às posições/letras: A=1, B=2, C=3, D=4, E=5.
#define LED_PIN_A D3  // GPIO0  - LED Verde   (Posição 1 / Letra A)
#define LED_PIN_B D2  // GPIO4  - LED Amarelo (Posição 2 / Letra B)
#define LED_PIN_C D5  // GPIO14 - LED Vermelho(Posição 3 / Letra C)
#define LED_PIN_D D1  // GPIO5  - LED Azul    (Posição 4 / Letra D)
#define LED_PIN_E D7  // GPIO13 - LED Branco  (Posição 5 / Letra E)

#define LED_COUNT 5

// ========================================
// TIMINGS DOS PADRÕES DE LED (ms)
// ========================================
// A) Conexão / reconexão: LEDs das pontas (A e E) piscam juntos rápido
#define LED_CONN_BLINK_MS 150

// A) Ocioso (conectado, sem resposta): heartbeat discreto no LED A
#define LED_IDLE_PERIOD_MS 2000  // 1 pulso a cada ~2s
#define LED_IDLE_PULSE_MS 80     // duração do pulso

// B) Evento de teste (questionType="test"): chase A->E
#define LED_CHASE_STEP_MS 120    // tempo de cada LED no chase
#define LED_CHASE_PASSES 2       // repete a varredura 2x

// B) Erro (hasData=false / letra inválida): 5 LEDs piscando juntos
#define LED_ERROR_ON_MS 250
#define LED_ERROR_OFF_MS 250
#define LED_ERROR_CYCLES 3       // pisca 3x

// C) Sequência (yesno / dropdown / ordering / matching)
#define LED_SEQ_STEP_MS 1500     // duração de cada passo
#define LED_SEQ_GAP_MS 400       // intervalo (tudo apagado) entre passos
#define LED_SEQ_BLINK_MS 200     // período do piscar rápido (resposta "Não")
#define LED_SEQ_PASSES 2         // toca a sequência + 1 repetição
#define LED_SEQ_ERRBLINK_MS 250  // blink dos 5 juntos p/ slot inválido

// ========================================
// CONFIGURAÇÕES SERIAL
// ========================================
#define SERIAL_BAUD_RATE 115200

// ========================================
// CONFIGURAÇÕES JSON (ArduinoJson v6)
// ========================================
#define JSON_DOC_SIZE 512     // documento filtrado (sem explanation)
#define JSON_FILTER_SIZE 256  // documento do filtro

#endif // CONFIG_H
