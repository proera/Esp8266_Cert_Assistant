/*
 * LedController.cpp
 *
 * Implementação da máquina de estados da barra (6 respostas + 2 status).
 *
 * A lógica das respostas é a mesma do D1 Mini (v2.x), estendida de 5 para 6
 * posições. O que era "padrão de conexão/ocioso/processando nos mesmos LEDs"
 * virou o canal de status dos pixels 6-7 (renderStatus), desde o M3.
 */

#include "LedController.h"

#define BIT(i) (1u << (i))
#define ALL_LEDS_MASK ((1u << LED_COUNT) - 1u)

// Cor de cada posição de resposta (A-F), na ordem dos pixels 0..LED_COUNT-1.
static const CRGB kPosColor[LED_COUNT] = {
  CRGB(LED_COLOR_A), CRGB(LED_COLOR_B), CRGB(LED_COLOR_C),
  CRGB(LED_COLOR_D), CRGB(LED_COLOR_E), CRGB(LED_COLOR_F),
};

void LedController::begin() {
  FastLED.addLeds<WS2812B, LED_BAR_PIN, GRB>(_bar, LED_BAR_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  // Teto de potência: um pico (ex.: erro = tudo aceso) acima disso derrubaria
  // a porta USB. O FastLED escala o brilho do frame inteiro para caber no teto.
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_MAX_VOLTS, LED_MAX_MILLIAMPS);

  fill_solid(_bar, LED_BAR_COUNT, CRGB::Black);
  _lastMask = 0xFF;  // força a 1ª transmissão
  _dirty = false;
  allOff();
  flush();

  _answerActive = false;
  _connected = false;
  _processing = false;
  _bootMillis = millis();
  _firstAnswerReceived = false;
  _blackoutAnnounced = false;
}

// ========================================
// Saída de baixo nível (frame; flush() transmite)
// ========================================
void LedController::writeMask(uint8_t mask) {
  if (mask == _lastMask) {
    return;  // nada mudou nos pixels de resposta
  }
  _lastMask = mask;

  for (uint8_t i = 0; i < LED_COUNT; i++) {
    _bar[i] = (mask & BIT(i)) ? kPosColor[i] : CRGB::Black;
  }
  _dirty = true;
}

void LedController::setStatusPixel(uint8_t pix, const CRGB& c) {
  if (_bar[pix] == c) {
    return;
  }
  _bar[pix] = c;
  _dirty = true;
}

void LedController::allOff() {
  writeMask(0);
}

void LedController::flush() {
  if (_dirty) {
    FastLED.show();
    _dirty = false;
  }
}

// ========================================
// Loop principal de animação
// ========================================
void LedController::update() {
  unsigned long now = millis();

  // Janela de boot: por LED_BOOT_BLINK_MS a barra sinaliza normalmente; depois
  // blackout TOTAL (respostas e status) até a 1ª resposta. O stream/serial
  // seguem ativos no blackout. (_answerActive implica _firstAnswerReceived,
  // então o blackout nunca interfere na exibição de uma resposta.)
  if (!_firstAnswerReceived && (now - _bootMillis) >= LED_BOOT_BLINK_MS) {
    if (!_blackoutAnnounced) {
      Serial.println(F("[LED] Janela de boot encerrada sem resposta -> barra apagada ate a 1a resposta"));
      _blackoutAnnounced = true;
    }
    allOff();
    setStatusPixel(LED_PIX_STATUS_CONN, CRGB::Black);
    setStatusPixel(LED_PIX_STATUS_PROC, CRGB::Black);
    flush();
    return;
  }

  // Canal de resposta (pixels 0-5)
  if (_answerActive) {
    switch (_mode) {
      case MODE_HOLD:  renderHold(now);   break;
      case MODE_CHASE: renderChase(now);  break;
      case MODE_ERROR: renderError(now);  break;
      case MODE_SEQ:   renderSeq(now);    break;
    }
  } else {
    allOff();
  }

  // Canal de status (pixels 6-7) — independente da resposta.
  renderStatus(now);
  flush();
}

void LedController::setConnected(bool connected) {
  // Stream caiu no meio de um "processando": aborta. O processando não tem
  // TTL — sem isto o pixel 7 piscaria indefinidamente escondendo a queda.
  // Ao reabrir, o status inicial do servidor ressincroniza o estado.
  if (!connected && _processing) {
    Serial.println(F("[LED] Stream caiu durante o processamento -> abortando pixel de solving"));
    _processing = false;
  }
  _connected = connected;
}

// ========================================
// Canal de status: pixel 6 (conexão) + pixel 7 (processamento)
// ========================================
void LedController::renderStatus(unsigned long now) {
  // Pixel 6 — conexão: âmbar piscando enquanto (re)conecta; heartbeat verde
  // discreto (pulso de LED_IDLE_PULSE_MS a cada LED_IDLE_PERIOD_MS) quando ok.
  CRGB conn;
  if (!_connected) {
    bool on = ((now / LED_CONN_BLINK_MS) % 2) == 0;
    conn = on ? CRGB(LED_COLOR_STATUS_CONN) : CRGB::Black;
  } else {
    unsigned long phase = now % LED_IDLE_PERIOD_MS;
    conn = (phase < LED_IDLE_PULSE_MS) ? CRGB(LED_COLOR_STATUS_OK) : CRGB::Black;
  }
  setStatusPixel(LED_PIX_STATUS_CONN, conn);

  // Pixel 7 — processamento: ciano piscando enquanto houver solving.
  CRGB proc = CRGB::Black;
  if (_processing) {
    bool on = ((now / LED_PROC_BLINK_MS) % 2) == 0;
    proc = on ? CRGB(LED_COLOR_STATUS_PROC) : CRGB::Black;
  }
  setStatusPixel(LED_PIX_STATUS_PROC, proc);
}

// ========================================
// D) Processando (status solving): pixel 7 até answer / error / idle
// ========================================
void LedController::showProcessing() {
  _processing = true;
  _firstAnswerReceived = true;  // encerra o blackout pós-boot, se ativo
}

void LedController::stopProcessing() {
  // Canal independente: encerrar o processando nunca toca na resposta em
  // exibição (era esta a razão do condicional que existia até a v3.1, quando
  // status e resposta dividiam os mesmos 5 LEDs).
  _processing = false;
}

// ========================================
// B) HOLD: resposta retida por LED_HOLD_TTL_MS, depois apaga.
//    Começa com um blank de LED_HOLD_INTAKE_MS (transição visível p/ respostas
//    iguais consecutivas). Conteúdo: _holdMask fixo + _holdBlinkMask piscando.
//    single/multiple => _holdBlinkMask == 0 (estático). yesno => "Não" pisca.
// ========================================
void LedController::startHold() {
  _answerActive = true;
  _firstAnswerReceived = true;  // encerra o blackout pós-boot, se ativo
  _mode = MODE_HOLD;
  _animStart = millis();
  allOff();  // começa pelo blank de chegada; renderHold conduz a partir daqui
}

void LedController::renderHold(unsigned long now) {
  unsigned long elapsed = now - _animStart;

  // Expirou: a resposta deixa de ser exibida.
  if (elapsed >= LED_HOLD_TTL_MS) {
    allOff();
    _answerActive = false;
    return;
  }
  // Blank de chegada: tudo apagado no início de toda resposta.
  if (elapsed < LED_HOLD_INTAKE_MS) {
    allOff();
    return;
  }
  // Exibição: letras/"Sim" fixos; "Não" do yesno piscando.
  if (_holdBlinkMask == 0) {
    writeMask(_holdMask);
    return;
  }
  bool on = ((now / LED_YESNO_BLINK_MS) % 2) == 0;
  writeMask(_holdMask | (on ? _holdBlinkMask : 0));
}

// ========================================
// B) test -> chase A->F
// ========================================
void LedController::showTestChase() {
  _answerActive = true;
  _firstAnswerReceived = true;  // encerra o blackout pós-boot, se ativo
  _mode = MODE_CHASE;
  _animStart = millis();
  allOff();
}

void LedController::renderChase(unsigned long now) {
  unsigned long elapsed = now - _animStart;
  unsigned long step = elapsed / LED_CHASE_STEP_MS;
  unsigned long total = (unsigned long)LED_COUNT * LED_CHASE_PASSES;

  if (step >= total) {
    allOff();
    _answerActive = false;  // terminou
    return;
  }
  writeMask(BIT(step % LED_COUNT));
}

// ========================================
// B) erro -> os 6 pixels de resposta piscando juntos
// ========================================
void LedController::showError() {
  _answerActive = true;
  _firstAnswerReceived = true;  // encerra o blackout pós-boot, se ativo
  _mode = MODE_ERROR;
  _animStart = millis();
  allOff();
}

void LedController::renderError(unsigned long now) {
  unsigned long elapsed = now - _animStart;
  const unsigned long period = LED_ERROR_ON_MS + LED_ERROR_OFF_MS;
  const unsigned long total = period * LED_ERROR_CYCLES;

  if (elapsed >= total) {
    allOff();
    _answerActive = false;
    return;
  }
  bool on = (elapsed % period) < LED_ERROR_ON_MS;
  writeMask(on ? ALL_LEDS_MASK : 0);
}

// ========================================
// B) single / multiple -> hold
// ========================================
void LedController::showSingle(uint8_t pos) {
  if (pos < 1 || pos > LED_COUNT) {
    showError();
    return;
  }
  _holdMask = BIT(pos - 1);
  _holdBlinkMask = 0;
  startHold();
}

void LedController::showMultiple(const uint8_t* pos, uint8_t n) {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (pos[i] >= 1 && pos[i] <= LED_COUNT) {
      mask |= BIT(pos[i] - 1);
    }
  }
  if (mask == 0) {
    showError();
    return;
  }
  _holdMask = mask;
  _holdBlinkMask = 0;
  startHold();
}

// ========================================
// C) Sequências (yesno / dropdown / ordering / matching)
// ========================================
void LedController::startSeq() {
  _answerActive = true;
  _firstAnswerReceived = true;  // encerra o blackout pós-boot, se ativo
  _mode = MODE_SEQ;
  _animStart = millis();
  allOff();
}

void LedController::showYesNo(const bool* flags, uint8_t n) {
  if (n == 0) {
    showError();
    return;
  }
  if (n > LED_COUNT) n = LED_COUNT;

  // Afirmação i -> pixel i, todas simultâneas e retidas até o próximo answer.
  // Sim = fixo (_holdMask); Não = piscando (_holdBlinkMask).
  uint8_t solid = 0;
  uint8_t blink = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (flags[i]) solid |= BIT(i);
    else          blink |= BIT(i);
  }

  _holdMask = solid;
  _holdBlinkMask = blink;
  startHold();  // blank de chegada + exibição por TTL; renderHold conduz a animação
}

void LedController::showSlots(const uint8_t* slots, uint8_t n) {
  if (n == 0) {
    showError();
    return;
  }
  if (n > LED_COUNT) n = LED_COUNT;

  _stepCount = n;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t v = slots[i];
    if (v >= 1 && v <= LED_COUNT) {
      _stepType[i] = STEP_SOLID;
      _stepLed[i] = v - 1;  // posição -> pixel
    } else {
      // Slot fora de 1..6: passo de erro curto (os 6 juntos piscam 1x).
      _stepType[i] = STEP_BLINKALL;
      _stepLed[i] = 0;
    }
  }
  startSeq();
}

void LedController::renderSeq(unsigned long now) {
  unsigned long elapsed = now - _animStart;
  const unsigned long slotLen = LED_SEQ_STEP_MS + LED_SEQ_GAP_MS;
  unsigned long idx = elapsed / slotLen;          // passo global (com repetições)
  unsigned long within = elapsed % slotLen;
  unsigned long totalSteps = (unsigned long)_stepCount * LED_SEQ_PASSES;

  if (idx >= totalSteps) {
    allOff();
    _answerActive = false;  // terminou
    return;
  }

  // Intervalo (gap) entre passos: tudo apagado.
  if (within >= LED_SEQ_STEP_MS) {
    allOff();
    return;
  }

  uint8_t s = idx % _stepCount;
  uint8_t led = _stepLed[s];

  switch (_stepType[s]) {
    case STEP_SOLID:
      writeMask(BIT(led));
      break;
    case STEP_BLINK: {
      bool on = ((within / LED_SEQ_BLINK_MS) % 2) == 0;
      writeMask(on ? BIT(led) : 0);
      break;
    }
    case STEP_BLINKALL: {
      // Pisca os 6 juntos 1x dentro do passo.
      writeMask(within < LED_SEQ_ERRBLINK_MS ? ALL_LEDS_MASK : 0);
      break;
    }
  }
}
