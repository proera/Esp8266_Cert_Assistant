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
  _bootMillis = millis();
  _firstAnswerReceived = false;
  _blackoutAnnounced = false;
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
      case MODE_HOLD:       renderHold(now);        break;
      case MODE_CHASE:      renderChase(now);       break;
      case MODE_ERROR:      renderError(now);       break;
      case MODE_SEQ:        renderSeq(now);         break;
      case MODE_PROCESSING: renderProcessing(now);  break;
    }
    return;
  }

  // Sem resposta ativa: padrão de conexão/ocioso.
  // Janela de boot: por LED_BOOT_BLINK_MS os LEDs sinalizam normalmente; depois
  // ficam apagados até a 1ª resposta. O stream/serial seguem ativos no blackout.
  if (!_firstAnswerReceived && (now - _bootMillis) >= LED_BOOT_BLINK_MS) {
    if (!_blackoutAnnounced) {
      Serial.println(F("[LED] Janela de boot encerrada sem resposta -> LEDs apagados ate a 1a resposta"));
      _blackoutAnnounced = true;
    }
    allOff();
    return;
  }

  if (_connected) {
    renderIdle(now);
  } else {
    renderConnecting(now);
  }
}

void LedController::setConnected(bool connected) {
  // Stream caiu no meio de um "processando": aborta. Senão o LED C piscaria
  // indefinidamente (o processando não tem TTL) escondendo a queda da conexão.
  // Ao reabrir, o status inicial do servidor ressincroniza o estado.
  if (!connected && _answerActive && _mode == MODE_PROCESSING) {
    Serial.println(F("[LED] Stream caiu durante o processamento -> abortando LED C"));
    allOff();
    _answerActive = false;
  }
  _connected = connected;
}

// ========================================
// D) Processando (status solving): LED C piscando até answer / error / idle
// ========================================
void LedController::showProcessing() {
  if (_answerActive && _mode == MODE_PROCESSING) {
    return;  // já processando: não reinicia o piscar (evita glitch visual)
  }
  _answerActive = true;
  _firstAnswerReceived = true;  // encerra o blackout pós-boot, se ativo
  _mode = MODE_PROCESSING;
  _animStart = millis();
  allOff();
}

void LedController::stopProcessing() {
  // Só encerra se o que está no ar é o "processando". Se for uma resposta,
  // ignora: no fluxo normal o status idle chega logo após o answer.
  if (_answerActive && _mode == MODE_PROCESSING) {
    allOff();
    _answerActive = false;
  }
}

void LedController::renderProcessing(unsigned long now) {
  // Sem TTL: pisca até um answer/error/idle chegar (ou o stream cair).
  bool on = ((now / LED_PROC_BLINK_MS) % 2) == 0;
  writeMask(on ? BIT(LED_PROC_INDEX) : 0);
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
// B) HOLD: resposta retida por LED_HOLD_TTL_MS, depois volta ao heartbeat.
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

  // Expirou: a resposta deixa de ser exibida e a saúde da conexão reassume.
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
// B) test -> chase A->E
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

  // Afirmação i -> LED i, todas simultâneas e retidas até o próximo answer.
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
