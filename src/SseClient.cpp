/*
 * SseClient.cpp
 *
 * Implementação do cliente SSE não-bloqueante.
 */

#include "SseClient.h"

static const unsigned long kBackoffTable[] = STREAM_BACKOFF_TABLE;
static const uint8_t kBackoffCount = sizeof(kBackoffTable) / sizeof(kBackoffTable[0]);

void SseClient::begin(AnswerCallback cb) {
  _cb = cb;
  _state = ST_DISCONNECTED;
  _nextAttempt = 0;       // tenta conectar assim que houver WiFi
  _backoffIdx = 0;
  _lineLen = 0;
  _lineOverflow = false;
  _dataLen = 0;
  _eventIsAnswer = false;
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
  _lineLen = 0;
  _lineOverflow = false;
  _dataLen = 0;
  _eventIsAnswer = false;
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
  _eventIsAnswer = false;
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
// Leitura incremental: bytes -> linhas
// ========================================
void SseClient::pump(bool headerPhase) {
  while (_client.available()) {
    char c = (char)_client.read();
    _lastActivity = millis();  // qualquer byte recebido conta como atividade

    if (c == '\r') {
      continue;  // aceita tanto \n quanto \r\n
    }

    if (c != '\n') {
      if (_lineLen < SSE_MAX_LINE - 1) {
        _lineBuf[_lineLen++] = c;
      } else {
        _lineOverflow = true;  // estoura o teto: descarta até a quebra de linha
      }
      continue;
    }

    // Quebra de linha => linha completa.
    if (_lineOverflow) {
      Serial.println(F("[SSE] Linha excedeu o teto — descartada"));
      _lineOverflow = false;
      _lineLen = 0;
      continue;
    }

    _lineBuf[_lineLen] = '\0';
    size_t len = _lineLen;
    _lineLen = 0;

    if (headerPhase) {
      processHeaderLine(_lineBuf, len);
      if (_state != ST_HEADERS) {
        return;  // transicionou para STREAMING ou agendou retry
      }
    } else {
      processSseLine(_lineBuf, len);
    }

    yield();  // alimenta o watchdog entre linhas
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
    Serial.println(F("[SSE] Stream aberto — recebendo eventos"));
    resetBackoff();  // conexão saudável: zera o backoff
    _state = ST_STREAMING;
    _eventIsAnswer = false;
    _dataLen = 0;
    return;
  }

  if (strncmp(line, "HTTP/", 5) == 0) {
    _statusOk = (strstr(line, " 200 ") != nullptr) || containsCI(line, "200 OK");
    Serial.print(F("[SSE] "));
    Serial.println(line);
  } else if (containsCI(line, "content-type") && containsCI(line, "text/event-stream")) {
    _ctEventStream = true;
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
    _eventIsAnswer = (strcmp(v, "answer") == 0);
    return;
  }

  if ((v = skipFieldPrefix(line, "data:")) != nullptr) {
    size_t vlen = strlen(v);
    // Concatena múltiplas linhas data: com '\n' (conforme spec SSE).
    size_t need = _dataLen + (_dataLen > 0 ? 1 : 0) + vlen;
    if (need >= SSE_MAX_DATA) {
      Serial.println(F("[SSE] Payload excedeu o teto — evento descartado"));
      _dataLen = 0;
      _eventIsAnswer = false;  // descarta este evento
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
  if (_eventIsAnswer && _dataLen > 0) {
    _dataBuf[_dataLen] = '\0';
    if (_cb) {
      _cb(_dataBuf);
    }
  }
  // Zera os buffers do evento.
  _eventIsAnswer = false;
  _dataLen = 0;
}
