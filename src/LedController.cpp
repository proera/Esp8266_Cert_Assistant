/*
 * LedController.cpp
 *
 * Implementação da máquina de estados dos 5 LEDs.
 */

#include "LedController.h"

#define BIT(i) (1u << (i))
#define ALL_LEDS_MASK ((1u << LED_COUNT) - 1u)

void LedController::begin() {
  _pins[0] = LED_PIN_A;
  _pins[1] = LED_PIN_B;
  _pins[2] = LED_PIN_C;
  _pins[3] = LED_PIN_D;
  _pins[4] = LED_PIN_E;

  for (uint8_t i = 0; i < LED_COUNT; i++) {
    pinMode(_pins[i], OUTPUT);
  }
  allOff();

  _answerActive = false;
  _connected = false;
}

// ========================================
// Saída de baixo nível
// ========================================
void LedController::writeMask(uint8_t mask) {
  for (uint8_t i = 0; i < LED_COUNT; i++) {
    digitalWrite(_pins[i], (mask & BIT(i)) ? HIGH : LOW);
  }
}

void LedController::allOff() {
  writeMask(0);
}

// ========================================
// Loop principal de animação
// ========================================
void LedController::update() {
  unsigned long now = millis();

  if (_answerActive) {
    switch (_mode) {
      case MODE_HOLD:                       break;  // mantém o que está aceso
      case MODE_CHASE: renderChase(now);    break;
      case MODE_ERROR: renderError(now);    break;
      case MODE_SEQ:   renderSeq(now);      break;
    }
    return;
  }

  // Sem resposta ativa: padrão de conexão/ocioso.
  if (_connected) {
    renderIdle(now);
  } else {
    renderConnecting(now);
  }
}

void LedController::setConnected(bool connected) {
  _connected = connected;
}

// ========================================
// A) Padrões de conexão (somente quando !_answerActive)
// ========================================
void LedController::renderConnecting(unsigned long now) {
  // Pontas (A e E) piscam juntas rápido.
  bool on = ((now / LED_CONN_BLINK_MS) % 2) == 0;
  writeMask(on ? (BIT(0) | BIT(LED_COUNT - 1)) : 0);
}

void LedController::renderIdle(unsigned long now) {
  // Heartbeat discreto: pulso curto no LED A a cada LED_IDLE_PERIOD_MS.
  unsigned long phase = now % LED_IDLE_PERIOD_MS;
  writeMask(phase < LED_IDLE_PULSE_MS ? BIT(0) : 0);
}

// ========================================
// B) test -> chase A->E
// ========================================
void LedController::showTestChase() {
  _answerActive = true;
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
    _answerActive = false;  // volta ao ocioso
    return;
  }
  writeMask(BIT(step % LED_COUNT));
}

// ========================================
// B) erro -> 5 LEDs piscando juntos
// ========================================
void LedController::showError() {
  _answerActive = true;
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
  _answerActive = true;
  _mode = MODE_HOLD;
  writeMask(_holdMask);
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
  _answerActive = true;
  _mode = MODE_HOLD;
  writeMask(_holdMask);
}

// ========================================
// C) Sequências (yesno / dropdown / ordering / matching)
// ========================================
void LedController::startSeq() {
  _answerActive = true;
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

  _stepCount = n;
  for (uint8_t i = 0; i < n; i++) {
    // Afirmação i -> LED i. Sim = fixo; Não = piscando.
    _stepType[i] = flags[i] ? STEP_SOLID : STEP_BLINK;
    _stepLed[i] = i;
  }
  startSeq();
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
      _stepLed[i] = v - 1;  // posição -> LED
    } else {
      // Slot fora de 1..5: passo de erro curto (5 juntos piscam 1x).
      _stepType[i] = STEP_BLINK5;
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
    _answerActive = false;  // terminou: volta ao ocioso
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
    case STEP_BLINK5: {
      // Pisca os 5 juntos 1x dentro do passo.
      writeMask(within < LED_SEQ_ERRBLINK_MS ? ALL_LEDS_MASK : 0);
      break;
    }
  }
}
