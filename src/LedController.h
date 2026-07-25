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
 *   3. test / erro / sequências (dropdown/ordering/matching) tocam e voltam ao ocioso.
 *   4. single / multiple / yesno ficam retidos por LED_HOLD_TTL_MS e então
 *      voltam ao heartbeat (yesno: Sim = fixo, Não = piscando, simultâneos).
 *      Toda resposta começa com um blank curto (LED_HOLD_INTAKE_MS) para tornar
 *      visível a chegada mesmo de respostas iguais consecutivas (ex.: A depois A).
 *   5. Conexão caindo/reconectando vence o estado ocioso.
 *   6. "Processando" (status solving => LED C piscando) vence resposta segurada e
 *      dura até chegar answer / status error / status idle. Se o stream cair no
 *      meio, é abortado — a saúde da conexão volta a ser sinalizada e o status
 *      inicial ressincroniza o estado quando o stream reabre.
 *
 * Janela de boot: após begin(), por LED_BOOT_BLINK_MS os LEDs sinalizam
 * conexão/ocioso normalmente; em seguida ficam apagados até a 1ª resposta do
 * backend (o stream/serial seguem ativos — só a saída dos LEDs é suprimida).
 * A 1ª resposta encerra o blackout em definitivo e o fluxo normal reassume.
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

    // --- Evento status (state="solving"/"idle") ---
    // Processando: LED C piscando até chegar answer / error / idle. Tem
    // prioridade sobre resposta segurada (uma nova requisição em andamento
    // significa que a resposta exibida está prestes a ser substituída).
    void showProcessing();
    // Encerra o "processando" (state="idle"). NÃO tem efeito se o que está
    // sendo exibido é uma resposta: no fluxo normal o idle chega logo após o
    // answer e não deve apagar a resposta.
    void stopProcessing();

    // --- Eventos de resposta (interrompem o que estiver tocando) ---
    void showSingle(uint8_t pos);                       // pos 1..5
    void showMultiple(const uint8_t* pos, uint8_t n);   // posições 1..5
    void showYesNo(const bool* flags, uint8_t n);       // n afirmações (<=5), retido
    void showSlots(const uint8_t* slots, uint8_t n);    // n slots, valor 1..5 cada
    void showTestChase();                               // varredura A->E
    void showError();                                   // 5 LEDs piscando juntos

  private:
    enum Mode { MODE_HOLD, MODE_CHASE, MODE_ERROR, MODE_SEQ, MODE_PROCESSING };

    // Tipo de passo numa sequência.
    enum StepType { STEP_SOLID, STEP_BLINK, STEP_BLINK5 };

    uint8_t _pins[LED_COUNT];

    bool _answerActive = false;   // false => exibe padrão de conexão/ocioso
    bool _connected = false;      // saúde do stream (quando !_answerActive)
    Mode _mode = MODE_HOLD;
    unsigned long _animStart = 0; // início da animação corrente

    // Janela de boot: blackout dos LEDs após LED_BOOT_BLINK_MS, até a 1ª resposta.
    unsigned long _bootMillis = 0;      // instante do begin() (início da janela)
    bool _firstAnswerReceived = false;  // 1ª resposta encerra o blackout pós-boot
    bool _blackoutAnnounced = false;    // log one-shot ao iniciar o blackout

    uint8_t _holdMask = 0;        // LEDs fixos (single/multiple + "Sim" do yesno)
    uint8_t _holdBlinkMask = 0;   // LEDs que piscam no HOLD ("Não" do yesno)

    // Buffer da sequência corrente (slots: dropdown / ordering / matching)
    StepType _stepType[LED_COUNT];
    uint8_t  _stepLed[LED_COUNT];
    uint8_t  _stepCount = 0;

    // Helpers de saída
    void writeMask(uint8_t mask);
    void allOff();

    // Renderizadores (sem bloquear)
    void renderConnecting(unsigned long now);
    void renderIdle(unsigned long now);
    void renderHold(unsigned long now);
    void renderChase(unsigned long now);
    void renderError(unsigned long now);
    void renderSeq(unsigned long now);
    void renderProcessing(unsigned long now);

    void startHold();
    void startSeq();
};

#endif // LED_CONTROLLER_H
