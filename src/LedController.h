/*
 * LedController.h
 *
 * Máquina de estados não-bloqueante dos 5 LEDs (A-E / posições 1-5).
 *
 * Não há LED de status dedicado: a saúde da conexão e as respostas do
 * backend compartilham os mesmos 5 LEDs, com padrões que não se confundem.
 *
 * Prioridade (ver spec, seção D):
 *   1. Resposta tem prioridade sobre conexão.
 *   2. Um answer novo sempre interrompe a exibição atual.
 *   3. test / erro / sequências tocam e voltam ao ocioso.
 *   4. single / multiple ficam acesos até o próximo answer.
 *   5. Conexão caindo/reconectando vence o estado ocioso.
 *
 * Todo o tempo é medido com millis() — nenhuma chamada a delay().
 */

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include "Config.h"

class LedController {
  public:
    void begin();

    // Chamar a cada iteração do loop(): avança a animação corrente.
    void update();

    // Saúde do stream. Só afeta a exibição quando NÃO há resposta ativa.
    void setConnected(bool connected);

    // --- Eventos de resposta (interrompem o que estiver tocando) ---
    void showSingle(uint8_t pos);                       // pos 1..5
    void showMultiple(const uint8_t* pos, uint8_t n);   // posições 1..5
    void showYesNo(const bool* flags, uint8_t n);       // n afirmações (<=5)
    void showSlots(const uint8_t* slots, uint8_t n);    // n slots, valor 1..5 cada
    void showTestChase();                               // varredura A->E
    void showError();                                   // 5 LEDs piscando juntos

  private:
    enum Mode { MODE_HOLD, MODE_CHASE, MODE_ERROR, MODE_SEQ };

    // Tipo de passo numa sequência.
    enum StepType { STEP_SOLID, STEP_BLINK, STEP_BLINK5 };

    uint8_t _pins[LED_COUNT];

    bool _answerActive = false;   // false => exibe padrão de conexão/ocioso
    bool _connected = false;      // saúde do stream (quando !_answerActive)
    Mode _mode = MODE_HOLD;
    unsigned long _animStart = 0; // início da animação corrente

    uint8_t _holdMask = 0;        // LEDs fixos para single/multiple

    // Buffer da sequência corrente (yesno / slots)
    StepType _stepType[LED_COUNT];
    uint8_t  _stepLed[LED_COUNT];
    uint8_t  _stepCount = 0;

    // Helpers de saída
    void writeMask(uint8_t mask);
    void allOff();

    // Renderizadores (sem bloquear)
    void renderConnecting(unsigned long now);
    void renderIdle(unsigned long now);
    void renderChase(unsigned long now);
    void renderError(unsigned long now);
    void renderSeq(unsigned long now);

    void startSeq();
};

#endif // LED_CONTROLLER_H
