/*
 * LedController.h
 *
 * Máquina de estados não-bloqueante da barra WS2812 (FastLED sobre RMT),
 * com dois canais independentes:
 *
 *   - Pixels 0-5: RESPOSTAS (posições A-F / 1-6), cada uma com a cor do LED
 *     físico que ocupava no D1 Mini (F = magenta; LED_COLOR_* no Config.h).
 *   - Pixel 6: CONEXÃO — âmbar piscando = conectando/reconectando; pulso
 *     verde curto = conectado e ocioso (heartbeat).
 *   - Pixel 7: PROCESSAMENTO — ciano piscando = solving; apagado = idle.
 *
 * Desde o M3 a saúde da conexão e o andamento do processamento NÃO disputam
 * mais pixels com as respostas — a escada de prioridades encolheu:
 *   1. Um answer novo sempre interrompe a exibição atual (nos pixels 0-5).
 *   2. test / erro / sequências (dropdown/ordering/matching) tocam e voltam
 *      ao apagado.
 *   3. single / multiple / yesno ficam retidos por LED_HOLD_TTL_MS (yesno:
 *      Sim = fixo, Não = piscando, simultâneos). Toda resposta começa com um
 *      blank curto (LED_HOLD_INTAKE_MS) para tornar visível a chegada mesmo
 *      de respostas iguais consecutivas (ex.: A depois A).
 *   4. O "processando" é um canal próprio (pixel 7): um solving novo NÃO
 *      apaga a resposta em exibição. Sem TTL; se o stream cair no meio, é
 *      abortado (senão piscaria para sempre) e o status inicial do servidor
 *      ressincroniza quando o stream reabre.
 *
 * Janela de boot: após begin(), por LED_BOOT_BLINK_MS a barra sinaliza
 * normalmente; em seguida TODOS os pixels (resposta e status) ficam apagados
 * até a 1ª resposta ou 1º solving (o stream/serial seguem ativos — só a saída
 * dos LEDs é suprimida). A 1ª resposta encerra o blackout em definitivo.
 *
 * Todo o tempo é medido com millis() — nenhuma chamada a delay().
 */

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>
#include "Config.h"

class LedController {
  public:
    void begin();

    // Chamar a cada iteração do loop(): avança a animação corrente.
    void update();

    // Saúde do stream (pixel 6). Aborta o "processando" quando cai.
    void setConnected(bool connected);

    // --- Evento status (state="solving"/"idle") ---
    // Processando: pixel 7 piscando até chegar answer / error / idle. Canal
    // independente: não toca na resposta exibida nos pixels 0-5.
    void showProcessing();
    // Encerra o "processando" (state="idle" ou answer entregue).
    void stopProcessing();

    // --- Eventos de resposta (interrompem o que estiver tocando) ---
    void showSingle(uint8_t pos);                       // pos 1..6
    void showMultiple(const uint8_t* pos, uint8_t n);   // posições 1..6
    void showYesNo(const bool* flags, uint8_t n);       // n afirmações (<=6), retido
    void showSlots(const uint8_t* slots, uint8_t n);    // n slots, valor 1..6 cada
    void showTestChase();                               // varredura A->F
    void showError();                                   // 6 pixels de resposta piscando

  private:
    enum Mode { MODE_HOLD, MODE_CHASE, MODE_ERROR, MODE_SEQ };

    // Tipo de passo numa sequência.
    enum StepType { STEP_SOLID, STEP_BLINK, STEP_BLINKALL };

    // Framebuffer da barra inteira (respostas + status).
    CRGB _bar[LED_BAR_COUNT];
    // Última máscara de resposta aplicada + flag de frame sujo: writeMask()/
    // setStatusPixel() só marcam _dirty quando algo mudou de fato, e flush()
    // só chama FastLED.show() com o frame sujo — update() roda a cada iteração
    // do loop e retransmitir o barramento WS2812 continuamente seria puro
    // desperdício. 0xFF = "nunca escreveu" (força a 1ª transmissão).
    uint8_t _lastMask = 0xFF;
    bool _dirty = false;

    bool _answerActive = false;   // há resposta em exibição nos pixels 0-5
    bool _connected = false;      // saúde do stream (pixel 6)
    bool _processing = false;     // solving em andamento (pixel 7)
    Mode _mode = MODE_HOLD;
    unsigned long _animStart = 0; // início da animação corrente

    // Janela de boot: blackout total após LED_BOOT_BLINK_MS, até a 1ª resposta.
    unsigned long _bootMillis = 0;      // instante do begin() (início da janela)
    bool _firstAnswerReceived = false;  // 1ª resposta encerra o blackout pós-boot
    bool _blackoutAnnounced = false;    // log one-shot ao iniciar o blackout

    uint8_t _holdMask = 0;        // pixels fixos (single/multiple + "Sim" do yesno)
    uint8_t _holdBlinkMask = 0;   // pixels que piscam no HOLD ("Não" do yesno)

    // Buffer da sequência corrente (slots: dropdown / ordering / matching)
    StepType _stepType[LED_COUNT];
    uint8_t  _stepLed[LED_COUNT];
    uint8_t  _stepCount = 0;

    // Helpers de saída (só marcam o frame; flush() transmite)
    void writeMask(uint8_t mask);
    void setStatusPixel(uint8_t pix, const CRGB& c);
    void allOff();
    void flush();

    // Renderizadores (sem bloquear)
    void renderHold(unsigned long now);
    void renderChase(unsigned long now);
    void renderError(unsigned long now);
    void renderSeq(unsigned long now);
    void renderStatus(unsigned long now);

    void startHold();
    void startSeq();
};

#endif // LED_CONTROLLER_H
