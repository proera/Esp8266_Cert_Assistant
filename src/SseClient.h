/*
 * SseClient.h
 *
 * Cliente SSE (Server-Sent Events) não-bloqueante para ESP8266.
 *
 * Abre UMA conexão GET HTTP/1.1 (texto claro, sem TLS) contra o endpoint de
 * stream e a mantém viva indefinidamente, lendo eventos incrementalmente.
 * Não usa HTTPClient (que espera o corpo terminar) — monta o GET na mão e lê
 * via WiFiClient byte a byte.
 *
 * No fim de cada evento "answer", chama o callback registrado com o payload
 * (JSON) acumulado dos campos data:.
 *
 * Reconecta automaticamente com backoff progressivo se o socket cair, o WiFi
 * cair, ou nenhuma linha chegar dentro de STREAM_TIMEOUT_MS.
 */

#ifndef SSE_CLIENT_H
#define SSE_CLIENT_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "Config.h"

class SseClient {
  public:
    // Recebe o buffer MUTÁVEL do payload (char*) — permite ao ArduinoJson
    // parsear em modo zero-copy (sem copiar strings/chaves no pool do documento).
    typedef void (*AnswerCallback)(char* payload);

    void begin(AnswerCallback cb);

    // Chamar a cada iteração do loop(): gerencia conexão, parsing e reconexão.
    void loop();

    // true somente quando os headers já foram validados e o corpo SSE flui.
    bool isStreaming() const { return _state == ST_STREAMING; }

  private:
    enum State { ST_DISCONNECTED, ST_HEADERS, ST_STREAMING };

    WiFiClient _client;
    AnswerCallback _cb = nullptr;
    State _state = ST_DISCONNECTED;

    unsigned long _lastActivity = 0;  // última linha/byte recebido
    unsigned long _nextAttempt = 0;   // quando tentar (re)conectar
    uint8_t _backoffIdx = 0;          // índice na tabela de backoff
    unsigned long _lastPingLog = 0;

    // Buffer de linha (compartilhado entre fase de header e de stream)
    char _lineBuf[SSE_MAX_LINE];
    size_t _lineLen = 0;
    bool _lineOverflow = false;

    // Estado de validação dos headers
    bool _statusOk = false;
    bool _ctEventStream = false;

    // Estado do evento SSE corrente
    bool _eventIsAnswer = false;
    char _dataBuf[SSE_MAX_DATA];
    size_t _dataLen = 0;

    void tryConnect();
    void scheduleRetry(const char* reason);
    void closeConnection();
    void resetBackoff();

    void pump(bool headerPhase);          // lê bytes disponíveis -> linhas
    void processHeaderLine(const char* line, size_t len);
    void processSseLine(const char* line, size_t len);
    void dispatchEvent();
    void checkTimeout();
};

#endif // SSE_CLIENT_H
