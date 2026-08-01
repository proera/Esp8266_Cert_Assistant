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
// Desde o M6, SSID/senha (e host/porta/path do stream) são DEFAULTS de
// primeiro boot: overrides persistidos em NVS (CLI serial: `config set ...`)
// têm precedência — ver ConfigStore.{h,cpp}.
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

// Teto próprio para a fase de headers. Num half-open (o servidor aceita o TCP e
// nunca responde, sem FIN), _client.connected() segue true e o firmware ficaria
// preso em ST_HEADERS indefinidamente, com os LEDs em "conectando" para sempre.
// Headers de um backend na LAN chegam em milissegundos: 10s já é folga enorme, e
// bem mais curto que o teto do stream (que precisa acomodar o ping de 15s).
#define HEADERS_TIMEOUT_MS 10000UL

// Backoff progressivo de reconexão (ms): 1s -> 2s -> 5s -> 10s -> 20s -> 30s (máx)
#define STREAM_BACKOFF_TABLE { 1000UL, 2000UL, 5000UL, 10000UL, 20000UL, 30000UL }

// Tetos de buffer do parser SSE (descarta/reseta se exceder).
// 16 K (M2): no ESP8266 eram 4 K e um evento maior era descartado EM SILÊNCIO;
// com 320 K de RAM no S3, 2×16 K de estático custa pouco e acomoda payloads
// verbosos (answerText + explanation longos em UTF-8) com folga.
#define SSE_MAX_LINE 16384  // teto por linha recebida
#define SSE_MAX_DATA 16384  // teto do payload acumulado (campos data:)

// Teto do chunk-size do Transfer-Encoding: chunked. Guarda contra
// desalinhamento do framing (um hex lido de lixo daria um chunk gigante):
// acima disso o parser assume dessincronização e reabre o stream.
#define SSE_MAX_CHUNK 65535UL

// Replay pós-reconexão (M5): teto do id: rastreado (GUIDs têm 36 chars) e
// faixa sã para o retry: ditado pelo servidor — fora dela o valor é ignorado
// (um retry: 0 martelaria o backend; um retry: gigante pareceria stream morto).
#define SSE_MAX_EVENT_ID 64
#define SSE_RETRY_MIN_MS 250UL
#define SSE_RETRY_MAX_MS 60000UL

// ========================================
// CONFIGURAÇÕES DA BARRA DE LED (WS2812)
// ========================================
// Barra WS2812 de 8 pixels no GPIO 13 (FastLED sobre RMT), substituindo os
// 5 LEDs discretos do D1 Mini. Desde o M3: pixels 0-5 = respostas (A-F) e
// pixels 6-7 = status dedicado (conexão e processamento) — resposta e saúde
// da conexão não disputam mais os mesmos LEDs.
#define LED_BAR_PIN 13
#define LED_BAR_COUNT 8

#define LED_COUNT 6  // posições de resposta (A=1 .. F=6)

// Pixels de status (canal independente das respostas)
#define LED_PIX_STATUS_CONN 6  // conexão: âmbar piscando = (re)conectando; pulso violeta = ocioso
#define LED_PIX_STATUS_PROC 7  // apagado fora do solving (a varredura usa a barra inteira)

// Brilho global (0-255) e teto de potência do FastLED. O limite de 5 V/600 mA
// foi o validado no projeto de teste do M0 — 8 pixels em branco cheio puxariam
// ~480 mA e um pico acima disso pode derrubar a porta USB.
#define LED_BRIGHTNESS 96
#define LED_MAX_VOLTS 5
#define LED_MAX_MILLIAMPS 600

// Cor de cada posição (0xRRGGBB), reproduzindo os LEDs físicos do D1 Mini
// (F, sem equivalente no D1, ganhou magenta). Hex cru (e não CRGB::) para o
// Config.h não depender do FastLED.
#define LED_COLOR_A 0x00FF00  // Verde    (Posição 1 / Letra A)
#define LED_COLOR_B 0xFFFF00  // Amarelo  (Posição 2 / Letra B)
#define LED_COLOR_C 0xFF0000  // Vermelho (Posição 3 / Letra C)
#define LED_COLOR_D 0x0000FF  // Azul     (Posição 4 / Letra D)
#define LED_COLOR_E 0xFFFFFF  // Branco   (Posição 5 / Letra E)
#define LED_COLOR_F 0xFF00FF  // Magenta  (Posição 6 / Letra F)

// Cores do canal de status — nenhuma pode coincidir com uma cor de resposta
// (LED_COLOR_A..F), senão um pulso de status se leria como resposta.
#define LED_COLOR_STATUS_CONN 0xFF6000  // Âmbar: conectando/reconectando (pixel 6 piscando)
#define LED_COLOR_STATUS_OK   0x8000FF  // Violeta: heartbeat de ocioso (pulso curto no pixel 6)

// ========================================
// TIMINGS DOS PADRÕES DE LED (ms)
// ========================================
// A) Conexão / reconexão: pixel de status (6) piscando em âmbar
#define LED_CONN_BLINK_MS 150

// A) Ocioso (conectado): heartbeat discreto no pixel de status (6), em violeta.
// Suprimido enquanto uma resposta está em exibição (o pulso ao lado da
// resposta desviava a leitura); volta quando o HOLD/sequência termina.
#define LED_IDLE_PERIOD_MS 2000  // 1 pulso a cada ~2s
#define LED_IDLE_PULSE_MS 80     // duração do pulso

// A) Janela de boot: após ligar, os LEDs sinalizam conexão/ocioso normalmente
//    por este período; em seguida ficam APAGADOS até a 1ª resposta do backend.
//    O stream segue ativo (ping + logs na serial) durante o blackout — só a
//    saída dos LEDs é suprimida. A 1ª resposta encerra o blackout em definitivo
//    e o fluxo normal (conexão/ocioso/resposta) reassume.
#define LED_BOOT_BLINK_MS 300000UL  // 5 min sinalizando no boot, depois apaga

// B) Evento de teste (questionType="test"): chase A->F
#define LED_CHASE_STEP_MS 120    // tempo de cada LED no chase
#define LED_CHASE_PASSES 2       // repete a varredura 2x

// B) Erro (hasData=false / letra inválida): os 6 pixels de resposta piscando juntos
#define LED_ERROR_ON_MS 250
#define LED_ERROR_OFF_MS 250
#define LED_ERROR_CYCLES 3       // pisca 3x

// C) Sequência (dropdown / ordering / matching)
#define LED_SEQ_STEP_MS 1500     // duração de cada passo
#define LED_SEQ_GAP_MS 400       // intervalo (tudo apagado) entre passos
#define LED_SEQ_BLINK_MS 200     // período do piscar rápido (resposta "Não")
#define LED_SEQ_PASSES 2         // toca a sequência + 1 repetição
#define LED_SEQ_ERRBLINK_MS 250  // blink dos 6 juntos p/ slot inválido

// D) Processando (evento status, state="solving"): varredura vermelha
// vai-e-vem (estilo Larson scanner) na BARRA INTEIRA (8 pixels), com rastro
// em fading atrás da cabeça, sem TTL, até chegar answer / status error /
// status idle. A varredura TOMA a barra enquanto durar (a resposta em
// exibição e o pixel de conexão reassumem ao terminar). O vermelho coincide
// de propósito com LED_COLOR_C: o movimento contínuo é inconfundível com uma
// resposta estática.
#define LED_SOLVE_COLOR 0xFF0000  // vermelho da varredura
#define LED_SOLVE_SPAN 8          // pixels varridos (a barra inteira)
#define LED_SOLVE_STEP_MS 80      // avanço da cabeça (ms por pixel)
#define LED_SOLVE_TRAIL 3         // pixels de rastro atrás da cabeça
#define LED_SOLVE_FADE 110        // brilho remanescente por passo do rastro (0-255)

// B) HOLD (single / multiple / yesno): resposta retida com TTL.
// Toda exibição começa com um blank curto (transição visível mesmo p/ respostas
// iguais consecutivas, ex.: A depois A), fica exibida por LED_HOLD_TTL_MS e
// então volta ao heartbeat. No yesno: Sim = fixo, Não = piscando (simultâneos).
#define LED_HOLD_TTL_MS 12000UL  // tempo que a resposta fica exibida
#define LED_HOLD_INTAKE_MS 250   // blank de chegada (tudo apagado) no início
#define LED_YESNO_BLINK_MS 350   // período do piscar do "Não" no yesno

// ========================================
// OTA (M6) — gravação pela rede via espota/ArduinoOTA
// ========================================
// A placa se anuncia por mDNS como <OTA_HOSTNAME>.local. A senha protege
// contra um flash acidental/indevido vindo da LAN; versionada em texto plano
// como as demais credenciais (decisão do mantenedor). Vazia = sem senha.
#define OTA_HOSTNAME "certmind-s3"
#define OTA_PASSWORD "certmind"

// ========================================
// CONFIGURAÇÕES SERIAL
// ========================================
#define SERIAL_BAUD_RATE 115200

// ========================================
// CONFIGURAÇÕES JSON (ArduinoJson v6)
// ========================================
// 4096 (M2): no ESP8266 eram 512 bytes e só cabia com um filtro que descartava
// o campo mais longo (explanation). Sem filtro, o documento vê o payload
// inteiro; o parse segue zero-copy (strings Linked no buffer mutável), então o
// pool carrega essencialmente a estrutura, não as strings.
#define JSON_DOC_SIZE 4096

#endif // CONFIG_H
