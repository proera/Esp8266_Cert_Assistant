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
  _capturing = false;
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

void LedController::setPixel(uint8_t pix, const CRGB& c) {
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
    setPixel(LED_PIX_STATUS_CONN, CRGB::Black);
    setPixel(LED_PIX_STATUS_PROC, CRGB::Black);
    flush();
    return;
  }

  // Solving: a varredura vermelha toma a barra inteira (8 pixels) enquanto
  // durar; o que estiver em exibição reassume quando terminar.
  if (_processing) {
    renderSolving(now);
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
  // A multicaptura, ao contrário, SOBREVIVE à queda de propósito: ela também
  // não tem TTL, mas o operador segue com um print parqueado no servidor e
  // precisa continuar vendo o aviso enquanto o firmware reconecta. O pixel 6
  // (âmbar piscando) já sinaliza a queda, então nada fica escondido — e o
  // status inicial do stream reaberto ressincroniza o estado real.
  _connected = connected;
}

// ========================================
// Canal de status: pixel 6 (conexão) e pixel 7 (multicaptura)
// ========================================
void LedController::renderStatus(unsigned long now) {
  // Pixel 6 — conexão: âmbar piscando enquanto (re)conecta; heartbeat violeta
  // discreto (pulso de LED_IDLE_PULSE_MS a cada LED_IDLE_PERIOD_MS) quando ok.
  // Com uma resposta em exibição o heartbeat fica suprimido (o pulso ao lado
  // da resposta desviava a leitura); a queda da conexão continua sinalizada.
  // Com a multicaptura ativa ele também some: o capturing É o novo estado-base
  // no lugar do ocioso, e a respiração do pixel 7 já é o sinal de vida.
  CRGB conn;
  if (!_connected) {
    bool on = ((now / LED_CONN_BLINK_MS) % 2) == 0;
    conn = on ? CRGB(LED_COLOR_STATUS_CONN) : CRGB::Black;
  } else if (_answerActive || _capturing) {
    conn = CRGB::Black;
  } else {
    unsigned long phase = now % LED_IDLE_PERIOD_MS;
    conn = (phase < LED_IDLE_PULSE_MS) ? CRGB(LED_COLOR_STATUS_OK) : CRGB::Black;
  }
  setPixel(LED_PIX_STATUS_CONN, conn);

  // Pixel 7 — multicaptura: respiração ciano enquanto o servidor aguarda a
  // próxima foto; apagado fora dela. Durante o solving update() nem chega aqui
  // (desvia para renderSolving, que varre a barra inteira) — e um solving já
  // teria encerrado a multicaptura de qualquer forma.
  setPixel(LED_PIX_STATUS_PROC, _capturing ? captureColor(now) : CRGB::Black);
}

// ========================================
// E) Multicaptura (status capturing): respiração ciano no pixel 7
// ========================================
void LedController::showCapturing() {
  if (!_capturing) {
    Serial.println(F("[LED] Multicaptura ativa -> respiracao ciano no pixel 7"));
  }
  // O capturing SUBSTITUI o idle final do solve (o servidor não manda os dois):
  // se a varredura não fosse encerrada aqui, ela rodaria para sempre — não tem
  // TTL e o idle que a encerraria nunca chega. Era o bug da v3.7.
  stopSolving();
  _capturing = true;
  _firstAnswerReceived = true;  // encerra o blackout pós-boot, se ativo
}

void LedController::stopCapturing() {
  _capturing = false;
}

// Encerra só a varredura de solving, preservando a resposta em exibição (se o
// TTL ainda corre) e a multicaptura. Chamado por TODO evento que fecha um solve
// — idle, answer (real ou test), erro e capturing — porque a varredura não tem
// TTL: o que não a encerra explicitamente a deixa rodando.
void LedController::stopSolving() {
  _processing = false;
}

// Nível da respiração no instante `now`. O tempo é quantizado em passos de
// LED_CAPTURE_STEP_MS ANTES de virar fase: sem isso o nível mudaria a cada tick
// de 5 ms da uiTask e o flush() retransmitiria a barra a ~200 Hz (o contrato da
// classe é só transmitir quando o frame muda de fato).
CRGB LedController::captureColor(unsigned long now) const {
  const unsigned long frames = LED_CAPTURE_PERIOD_MS / LED_CAPTURE_STEP_MS;
  const unsigned long frame = (now / LED_CAPTURE_STEP_MS) % frames;
  // sin8() dá a curva suave (vale -> pico -> vale) num ciclo completo da fase.
  const uint8_t wave = sin8((uint8_t)((frame * 256UL) / frames));
  CRGB c(LED_CAPTURE_COLOR);
  c.nscale8(lerp8by8(LED_CAPTURE_MIN, LED_CAPTURE_MAX, wave));
  return c;
}

// ========================================
// D) Processando (status solving): varredura vermelha vai-e-vem com rastro
//    na barra inteira, até answer / error / idle
// ========================================
void LedController::showProcessing() {
  if (!_processing) {
    _solveStart = millis();  // eventos "solving" repetidos não reiniciam a varredura
  }
  _processing = true;
  _firstAnswerReceived = true;  // encerra o blackout pós-boot, se ativo
  stopCapturing();  // a foto que fecha o conjunto chegou: o modo multicaptura acabou
}

void LedController::renderSolving(unsigned long now) {
  const uint8_t span = LED_SOLVE_SPAN;
  const uint8_t period = 2 * (span - 1);  // ida + volta, sem repetir as pontas
  unsigned long step = (now - _solveStart) / LED_SOLVE_STEP_MS;

  // Brilho por pixel: a cabeça (passo atual) a 255 e cada passo anterior
  // decaído por LED_SOLVE_FADE — é o rastro. O max() resolve a sobreposição
  // quando a cabeça inverte o sentido e passa por cima do próprio rastro.
  uint8_t level[LED_SOLVE_SPAN] = {0};
  uint8_t bright = 255;
  for (uint8_t k = 0; k <= LED_SOLVE_TRAIL && k <= step; k++) {
    uint8_t ph = (uint8_t)((step - k) % period);
    uint8_t pos = (ph < span) ? ph : (uint8_t)(period - ph);
    if (bright > level[pos]) level[pos] = bright;
    bright = scale8(bright, LED_SOLVE_FADE);
  }

  for (uint8_t i = 0; i < span; i++) {
    CRGB c(LED_SOLVE_COLOR);
    c.nscale8(level[i]);
    setPixel(i, c);
  }
  // A varredura escreve os pixels de resposta por fora do writeMask(): o
  // cache precisa ser invalidado para o canal de resposta redesenhar ao
  // reassumir (0xFF = sentinel de "força retransmissão").
  _lastMask = 0xFF;
}

void LedController::stopProcessing() {
  // Encerrar o solving não mexe no estado da resposta: se havia uma em
  // exibição (TTL ainda correndo), ela reassume os pixels 0-5 no próximo
  // update(); o pixel de conexão volta junto.
  stopSolving();
  // Este é o caminho do status "idle" (lote limpo), que também encerra a
  // multicaptura pelo contrato do servidor.
  stopCapturing();
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
  stopSolving();    // o answer chegou: a varredura acabou (não espera o idle)
  stopCapturing();  // answer real: o conjunto foi resolvido
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
  stopSolving();  // o ACK do print chegou: a varredura do solve acabou
  // NÃO chama stopCapturing(): o chase de test é "print recebido" e PRECEDE
  // cada capturing no fluxo real (test -> capturing a cada foto parcial).
  // Limpar aqui só produziria um apagão do pixel 7 entre os dois eventos.
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
  stopSolving();    // o solve terminou (em falha): a varredura acabou
  stopCapturing();  // erro fecha o modo (contrato do servidor)
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
  stopSolving();    // o answer chegou: a varredura acabou (não espera o idle)
  stopCapturing();  // answer real: o conjunto foi resolvido
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
