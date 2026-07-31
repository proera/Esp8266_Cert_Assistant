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
 * O corpo chega com Transfer-Encoding: chunked (o backend é Kestrel), então há
 * uma camada de de-framing: feedChunkedByte() consome o chunk-size e o CRLF
 * terminador de cada frame e entrega a feedLine() apenas os bytes do corpo
 * lógico. Servidores que respondem sem chunked seguem pelo caminho direto.
 *
 * No fim de cada evento "answer" ou "status", chama o callback correspondente
 * com o payload (JSON) acumulado dos campos data:. Outros nomes de evento são
 * descartados em silêncio.
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
    // Mesmo contrato de buffer, para o evento "status".
    typedef AnswerCallback StatusCallback;

    void begin(AnswerCallback onAnswer, StatusCallback onStatus);

    // Chamar a cada iteração do loop(): gerencia conexão, parsing e reconexão.
    void loop();

    // true somente quando os headers já foram validados e o corpo SSE flui.
    bool isStreaming() const { return _state == ST_STREAMING; }

  private:
    enum State { ST_DISCONNECTED, ST_HEADERS, ST_STREAMING };

    // Evento SSE corrente. EVT_NONE cobre "nenhum evento" e nomes desconhecidos
    // (descartados em silêncio — novos eventos podem surgir no servidor).
    enum EventType { EVT_NONE, EVT_ANSWER, EVT_STATUS };

    // Passos do de-framing chunked: linha do chunk-size (hex) -> bytes de
    // dados -> CRLF terminador -> próximo chunk-size.
    enum ChunkState { CHUNK_SIZE, CHUNK_DATA, CHUNK_CRLF };

    WiFiClient _client;
    AnswerCallback _onAnswer = nullptr;
    StatusCallback _onStatus = nullptr;
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

    // De-framing do corpo. O backend (Kestrel) serve o SSE com
    // Transfer-Encoding: chunked, então o corpo na conexão NÃO é o corpo lógico:
    // cada bloco vem prefixado pelo tamanho em hex e sufixado por CRLF. Sem
    // desmontar esses frames, o chunk-size aparece como linha solta (ignorada
    // por sorte, pois não casa com nenhum campo SSE) e o CRLF terminador vira
    // uma linha vazia espúria — que encerra o evento antes da hora sempre que um
    // chunk termina no meio de uma linha "data:", truncando o payload.
    bool _chunked = false;                  // Transfer-Encoding: chunked nos headers
    ChunkState _chunkState = CHUNK_SIZE;
    uint32_t _chunkRemaining = 0;           // bytes de dados restantes no chunk
    bool _chunkInExt = false;               // dentro de chunk-extension (após ';')
    bool _chunkHasDigits = false;           // a linha de size tem ao menos 1 hex

    // Estado do evento SSE corrente
    EventType _evtType = EVT_NONE;
    char _dataBuf[SSE_MAX_DATA];
    size_t _dataLen = 0;

    void tryConnect();
    void scheduleRetry(const char* reason);
    void closeConnection();
    void resetBackoff();

    void pump(bool headerPhase);          // lê bytes disponíveis -> linhas
    void feedLine(char c, bool headerPhase);  // byte do corpo lógico -> linha
    void feedChunkedByte(char c);         // desmonta o frame; dados -> feedLine
    void beginChunkSize();                // rearma a leitura de um chunk-size
    void processHeaderLine(const char* line, size_t len);
    void processSseLine(const char* line, size_t len);
    void dispatchEvent();
    // Teto por fase: headers e stream têm limites diferentes (ver Config.h).
    void checkTimeout(unsigned long limitMs, const char* reason);
};

#endif // SSE_CLIENT_H
