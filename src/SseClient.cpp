/*
 * SseClient.cpp
 *
 * Implementação do cliente SSE não-bloqueante.
 */

#include "SseClient.h"

static const unsigned long kBackoffTable[] = STREAM_BACKOFF_TABLE;
static const uint8_t kBackoffCount = sizeof(kBackoffTable) / sizeof(kBackoffTable[0]);

void SseClient::begin(AnswerCallback onAnswer, StatusCallback onStatus) {
  _onAnswer = onAnswer;
  _onStatus = onStatus;
  _state = ST_DISCONNECTED;
  _nextAttempt = 0;       // tenta conectar assim que houver WiFi
  _backoffIdx = 0;
  _lineLen = 0;
  _lineOverflow = false;
  _dataLen = 0;
  _evtType = EVT_NONE;
  _chunked = false;
  beginChunkSize();
}

// ========================================
// Loop principal
// ========================================
void SseClient::loop() {
  // Sem WiFi: nada a fazer além de garantir o socket fechado.
  if (WiFi.status() != WL_CONNECTED) {
    if (_state != ST_DISCONNECTED) {
      Serial.println(F("[SSE] WiFi caiu — fechando stream"));
      closeConnection();
      _state = ST_DISCONNECTED;
      _nextAttempt = millis();  // reconecta assim que o WiFi voltar
    }
    return;
  }

  switch (_state) {
    case ST_DISCONNECTED:
      if ((long)(millis() - _nextAttempt) >= 0) {
        tryConnect();
      }
      break;

    case ST_HEADERS:
      pump(true);
      if (_state == ST_HEADERS && !_client.connected() && !_client.available()) {
        scheduleRetry("socket fechou durante headers");
      }
      break;

    case ST_STREAMING:
      pump(false);
      if (_state == ST_STREAMING) {
        if (!_client.connected() && !_client.available()) {
          scheduleRetry("socket fechou");
        } else {
          checkTimeout();
        }
      }
      break;
  }
}

// ========================================
// Conexão
// ========================================
void SseClient::tryConnect() {
  Serial.print(F("[SSE] Conectando a "));
  Serial.print(STREAM_HOST);
  Serial.print(':');
  Serial.print(STREAM_PORT);
  Serial.println(STREAM_PATH);

  _client.stop();

  if (!_client.connect(STREAM_HOST, STREAM_PORT)) {
    scheduleRetry("falha no connect TCP");
    return;
  }

  // Monta o GET HTTP/1.1 manualmente.
  _client.print(F("GET "));
  _client.print(STREAM_PATH);
  _client.print(F(" HTTP/1.1\r\nHost: "));
  _client.print(STREAM_HOST);
  _client.print(':');
  _client.print(STREAM_PORT);
  _client.print(F("\r\nAccept: text/event-stream\r\n"
                  "Connection: keep-alive\r\n"
                  "User-Agent: ESP8266-CertMind\r\n\r\n"));

  // Prepara a fase de leitura de headers.
  _state = ST_HEADERS;
  _statusOk = false;
  _ctEventStream = false;
  _chunked = false;
  _lineLen = 0;
  _lineOverflow = false;
  _dataLen = 0;
  _evtType = EVT_NONE;
  beginChunkSize();
  _lastActivity = millis();
}

void SseClient::scheduleRetry(const char* reason) {
  unsigned long wait = kBackoffTable[_backoffIdx];
  Serial.print(F("[SSE] Reconectando em "));
  Serial.print(wait / 1000.0, 1);
  Serial.print(F("s ("));
  Serial.print(reason);
  Serial.println(F(")"));

  closeConnection();
  _state = ST_DISCONNECTED;
  _nextAttempt = millis() + wait;

  if (_backoffIdx + 1 < kBackoffCount) {
    _backoffIdx++;
  }
}

void SseClient::closeConnection() {
  _client.stop();
  _lineLen = 0;
  _lineOverflow = false;
  _dataLen = 0;
  _evtType = EVT_NONE;
  beginChunkSize();
}

void SseClient::resetBackoff() {
  _backoffIdx = 0;
}

void SseClient::checkTimeout() {
  if (millis() - _lastActivity > STREAM_TIMEOUT_MS) {
    scheduleRetry("timeout sem dados");
  }
}

// ========================================
// Leitura incremental: bytes -> (de-framing) -> linhas
// ========================================
void SseClient::pump(bool headerPhase) {
  const State expected = headerPhase ? ST_HEADERS : ST_STREAMING;

  while (_client.available()) {
    char c = (char)_client.read();
    _lastActivity = millis();  // qualquer byte recebido conta como atividade

    if (headerPhase) {
      feedLine(c, true);  // headers nunca são chunked
    } else if (_chunked) {
      feedChunkedByte(c);
    } else {
      feedLine(c, false);
    }

    if (_state != expected) {
      return;  // transicionou (headers prontos) ou agendou retry
    }
  }
}

// Monta linhas a partir do fluxo de bytes do corpo LÓGICO (já desmoldurado, se
// chunked) ou dos headers, e despacha cada linha completa.
void SseClient::feedLine(char c, bool headerPhase) {
  if (c == '\r') {
    return;  // aceita tanto \n quanto \r\n
  }

  if (c != '\n') {
    if (_lineLen < SSE_MAX_LINE - 1) {
      _lineBuf[_lineLen++] = c;
    } else {
      _lineOverflow = true;  // estoura o teto: descarta até a quebra de linha
    }
    return;
  }

  // Quebra de linha => linha completa.
  if (_lineOverflow) {
    Serial.println(F("[SSE] Linha excedeu o teto — descartada"));
    _lineOverflow = false;
    _lineLen = 0;
    return;
  }

  _lineBuf[_lineLen] = '\0';
  size_t len = _lineLen;
  _lineLen = 0;

  if (headerPhase) {
    processHeaderLine(_lineBuf, len);
  } else {
    processSseLine(_lineBuf, len);
  }

  yield();  // alimenta o watchdog entre linhas
}

// ========================================
// De-framing do Transfer-Encoding: chunked (RFC 9112 §7.1)
// ========================================
// Formato na conexão: "<size-hex>[;ext]\r\n<size bytes de dados>\r\n" repetido,
// terminando com um chunk de tamanho 0. Só os bytes de DADOS chegam ao montador
// de linhas — o size e o CRLF terminador são consumidos aqui. É justamente esse
// CRLF que, sem o de-framing, cortava a linha "data:" partida entre dois chunks.
void SseClient::beginChunkSize() {
  _chunkState = CHUNK_SIZE;
  _chunkRemaining = 0;
  _chunkInExt = false;
  _chunkHasDigits = false;
}

static int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void SseClient::feedChunkedByte(char c) {
  switch (_chunkState) {
    case CHUNK_SIZE:
      if (c == '\n') {
        if (!_chunkHasDigits) {
          // Linha sem nenhum dígito hex não é um chunk-size: é o trailer vazio
          // que fecha o last-chunk, ou ruído entre frames. Só rearma — tratar
          // isso como tamanho 0 dispararia um retry a mais (e o backoff pularia
          // um degrau).
          beginChunkSize();
        } else if (_chunkRemaining == 0) {
          // Chunk de tamanho 0 = fim do corpo: o servidor encerrou o stream
          // (que deveria ser infinito). Reabre a conexão.
          scheduleRetry("servidor encerrou o corpo (last-chunk)");
        } else if (_chunkRemaining > SSE_MAX_CHUNK) {
          scheduleRetry("chunk-size implausível — framing dessincronizado");
        } else {
          _chunkState = CHUNK_DATA;
        }
        return;
      }
      if (c == ';') {
        _chunkInExt = true;  // chunk-extension: ignora o resto da linha
        return;
      }
      if (!_chunkInExt) {
        int d = hexDigit(c);
        if (d >= 0) {
          _chunkRemaining = (_chunkRemaining << 4) | (uint32_t)d;
          _chunkHasDigits = true;
        }
        // Não-hex fora de extensão (ex.: o '\r') é ignorado.
      }
      return;

    case CHUNK_DATA:
      feedLine(c, false);  // byte do corpo lógico
      if (--_chunkRemaining == 0) {
        _chunkState = CHUNK_CRLF;
      }
      return;

    case CHUNK_CRLF:
      // Terminador do chunk: NÃO vai ao montador de linhas.
      if (c == '\n') {
        beginChunkSize();
      }
      return;
  }
}

// ========================================
// Fase de headers HTTP
// ========================================
static bool containsCI(const char* haystack, const char* needle) {
  // strstr case-insensitive simples (ASCII).
  size_t nlen = strlen(needle);
  if (nlen == 0) return true;
  for (const char* p = haystack; *p; p++) {
    size_t i = 0;
    while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
      i++;
    }
    if (i == nlen) return true;
  }
  return false;
}

void SseClient::processHeaderLine(const char* line, size_t len) {
  if (len == 0) {
    // Linha em branco => fim dos headers.
    if (!_statusOk) {
      scheduleRetry("status HTTP != 200");
      return;
    }
    if (!_ctEventStream) {
      Serial.println(F("[SSE] Aviso: Content-Type não é text/event-stream (seguindo mesmo assim)"));
    }
    Serial.print(F("[SSE] Stream aberto — recebendo eventos (framing: "));
    Serial.print(_chunked ? F("chunked") : F("identity"));
    Serial.println(F(")"));
    resetBackoff();  // conexão saudável: zera o backoff
    _state = ST_STREAMING;
    _evtType = EVT_NONE;
    _dataLen = 0;
    beginChunkSize();  // o corpo começa por um chunk-size (se chunked)
    return;
  }

  // Loga TODA linha de header. Sem isso só o status aparecia na serial, e
  // detalhes de transporte (ex.: Transfer-Encoding) ficavam invisíveis —
  // foi o que manteve a ausência do de-framing chunked escondida.
  Serial.print(F("[SSE] < "));
  Serial.println(line);

  if (strncmp(line, "HTTP/", 5) == 0) {
    _statusOk = (strstr(line, " 200 ") != nullptr) || containsCI(line, "200 OK");
  } else if (containsCI(line, "content-type") && containsCI(line, "text/event-stream")) {
    _ctEventStream = true;
  } else if (containsCI(line, "transfer-encoding") && containsCI(line, "chunked")) {
    _chunked = true;
  }
}

// ========================================
// Fase de stream SSE
// ========================================
static const char* skipFieldPrefix(const char* line, const char* field) {
  size_t flen = strlen(field);
  if (strncmp(line, field, flen) != 0) return nullptr;
  const char* v = line + flen;
  if (*v == ' ') v++;  // remove um único espaço após o ':'
  return v;
}

void SseClient::processSseLine(const char* line, size_t len) {
  if (len == 0) {
    // Linha em branco => fim do evento.
    dispatchEvent();
    return;
  }

  if (line[0] == ':') {
    // Comentário (prova de vida): ": connected" / ": ping". Log throttled.
    unsigned long now = millis();
    if (now - _lastPingLog > 30000UL) {
      _lastPingLog = now;
      Serial.print(F("[SSE] keep-alive"));
      Serial.println(line);
    }
    return;
  }

  const char* v;
  if ((v = skipFieldPrefix(line, "event:")) != nullptr) {
    if (strcmp(v, "answer") == 0) {
      _evtType = EVT_ANSWER;
    } else if (strcmp(v, "status") == 0) {
      _evtType = EVT_STATUS;
    } else {
      _evtType = EVT_NONE;  // evento desconhecido: descarta em silêncio
    }
    return;
  }

  if ((v = skipFieldPrefix(line, "data:")) != nullptr) {
    size_t vlen = strlen(v);
    // Concatena múltiplas linhas data: com '\n' (conforme spec SSE).
    size_t need = _dataLen + (_dataLen > 0 ? 1 : 0) + vlen;
    if (need >= SSE_MAX_DATA) {
      Serial.println(F("[SSE] Payload excedeu o teto — evento descartado"));
      _dataLen = 0;
      _evtType = EVT_NONE;  // descarta este evento
      return;
    }
    if (_dataLen > 0) {
      _dataBuf[_dataLen++] = '\n';
    }
    memcpy(_dataBuf + _dataLen, v, vlen);
    _dataLen += vlen;
    _dataBuf[_dataLen] = '\0';
    return;
  }

  // Outros campos (id:, retry:, ...) são ignorados.
}

void SseClient::dispatchEvent() {
  if (_dataLen > 0) {
    _dataBuf[_dataLen] = '\0';
    if (_evtType == EVT_ANSWER && _onAnswer) {
      _onAnswer(_dataBuf);
    } else if (_evtType == EVT_STATUS && _onStatus) {
      _onStatus(_dataBuf);
    }
  }
  // Zera os buffers do evento.
  _evtType = EVT_NONE;
  _dataLen = 0;
}
