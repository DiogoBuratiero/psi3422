#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>
#include <cmath>

// -----------------------------------------------------------------------------
// Configurações gerais
// -----------------------------------------------------------------------------
static const int TRANSFER_SIZE = 20;             // ex: "R:12.34 L:11.90"
static const long long RF_ADDR = 0xE7E7E7E7E7LL; // apenas referência

BufferedSerial pc(USBTX, USBRX, 115200);
nRF24L01P radio(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13);
DigitalOut ledRx(LED2);

// Motores (L298N)
DigitalOut in1(PTB0); // direita
DigitalOut in2(PTB1);
DigitalOut in3(PTB2); // esquerda
DigitalOut in4(PTB3);

// Encoders
InterruptIn encRight(PTA5); // encoder roda direita
InterruptIn encLeft(PTA1);  // encoder roda esquerda

volatile int32_t pulse_right = 0;
volatile int32_t pulse_left = 0;

// Calibrações (ajustados na prática)
static const float CM_PER_PULSE_RIGHT = 0.9f;
static const float CM_PER_PULSE_LEFT = 0.9f;

// Tempo de curva de 90° (ms) — calibrado empiricamente
static const int TURN_TIME_MS = 550;

// -----------------------------------------------------------------------------
// Sensor de ultrassom HC-SR04
// -----------------------------------------------------------------------------
DigitalOut trig(PTC7);  // Trigger
InterruptIn echo(PTA4); // Echo

Timer echoTimer; // base de tempo em µs
volatile uint32_t echo_start_us = 0;
volatile uint32_t echo_pulse_us = 0;
volatile bool new_measure = false;

// Última distância medida em cm (para ser usada na lógica do obstáculo)
float last_dist_cm = 1000.0f;

// Threshold de obstáculo em centímetros
static const float OBSTACLE_THRESHOLD_CM = 20.0f; // ajuste conforme desejar

// Envia pulso de trigger (~10 µs)
void send_trigger_pulse()
{
    trig = 0;
    wait_us(2);
    trig = 1;
    wait_us(10);
    trig = 0;
}

// Echo rising: início do pulso
void echo_rise()
{
    echo_start_us = echoTimer.read_us();
}

// Echo falling: fim do pulso
void echo_fall()
{
    uint32_t end_us = echoTimer.read_us();
    echo_pulse_us = end_us - echo_start_us;
    new_measure = true;
}

// -----------------------------------------------------------------------------
// Estados da trajetória
// -----------------------------------------------------------------------------
enum MoveState
{
    STATE_IDLE = 0,
    STATE_MOVE_X,
    STATE_TURN_90, // curva padrão 90° (direita) para o L original
    STATE_MOVE_Y,

    // Estados de desvio de obstáculo
    STATE_OBS_TURN_RIGHT, // gira 90° à direita ao detectar o obstáculo
    STATE_OBS_MOVE1,      // anda reto usando a "distância restante" em X
    STATE_OBS_TURN_LEFT,  // gira 90° à esquerda
    STATE_OBS_MOVE2       // anda reto a perna Y original
};

MoveState state = STATE_IDLE;
float targetX_cm = 0.0f;
float targetY_cm = 0.0f;

// variáveis de desvio
bool obstacle_active = false;
float obs_residualX = 0.0f; // quanto ainda faltava em X quando o obstáculo apareceu

// Timer global para movimento
Timer sysTimer;
int turn_start_ms = 0;

// -----------------------------------------------------------------------------
// printf simplificado
// -----------------------------------------------------------------------------
void pc_printf(const char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
    {
        if (n > (int)sizeof(buf))
            n = sizeof(buf);
        pc.write(buf, n);
    }
}

// -----------------------------------------------------------------------------
// Conversão float -> string "xx.xx" (sem usar %f)
// -----------------------------------------------------------------------------
void float_to_str(char *out, float v)
{
    int inteiro = (int)v;
    int frac = (int)((v - inteiro) * 100.0f);
    if (frac < 0)
        frac = -frac;
    snprintf(out, 16, "%d.%02d", inteiro, frac);
}

// Empacota int16 em 2 bytes (big-endian) no buffer
inline void put_i16(char *buf, int idx, int16_t v)
{
    buf[idx] = (char)((v >> 8) & 0xFF);
    buf[idx + 1] = (char)(v & 0xFF);
}

// -----------------------------------------------------------------------------
// Controle de motores
// -----------------------------------------------------------------------------
inline void motorA_forward()
{
    in1 = 0;
    in2 = 1;
} // direita frente

inline void motorA_backward()
{
    in1 = 1;
    in2 = 0;
} // direita ré

inline void motorA_stop()
{
    in1 = 0;
    in2 = 0;
}

inline void motorB_forward()
{
    in3 = 1;
    in4 = 0;
} // esquerda frente

inline void motorB_backward()
{
    in3 = 0;
    in4 = 1;
} // esquerda ré

inline void motorB_stop()
{
    in3 = 0;
    in4 = 0;
}

inline void ambos_forward()
{
    motorA_forward();
    motorB_forward();
}

inline void ambos_stop()
{
    motorA_stop();
    motorB_stop();
}

// -----------------------------------------------------------------------------
// Encoders
// -----------------------------------------------------------------------------
void reset_encoders()
{
    __disable_irq();
    pulse_right = 0;
    pulse_left = 0;
    __enable_irq();
}

// Timer para debounce dos encoders
Timer encDebounceTimer;
volatile uint32_t lastRight_us = 0;
volatile uint32_t lastLeft_us = 0;

// Tempo mínimo entre pulsos do mesmo encoder
static const uint32_t ENC_DEBOUNCE_US = 300;

void on_pulse_right()
{
    uint32_t now = encDebounceTimer.read_us();
    uint32_t dt = now - lastRight_us;


    if (dt >= ENC_DEBOUNCE_US)
    {
        pulse_right++;
        lastRight_us = now;
    }
}

void on_pulse_left()
{
    uint32_t now = encDebounceTimer.read_us();
    uint32_t dt = now - lastLeft_us;

    if (dt >= ENC_DEBOUNCE_US)
    {
        pulse_left++;
        lastLeft_us = now;
    }
}

// -----------------------------------------------------------------------------
// Rádio
// -----------------------------------------------------------------------------
void config_radio()
{
    radio.powerUp();
    thread_sleep_for(10);

    radio.setRfFrequency(2476); // 2.476 GHz
    radio.setAirDataRate(1000); // 1 Mbps
    radio.setRfOutputPower(0);  // 0 dBm

    const long long RX_ADDR = 0xA1A1A1A1A1LL; // cliente -> servidor (comandos/posições)
    const long long TX_ADDR = 0xB1B1B1B1B1LL; // servidor -> cliente (telemetria)

    radio.setTxAddress(TX_ADDR);
    radio.setRxAddress(RX_ADDR);

    radio.setTransferSize(TRANSFER_SIZE);

    // Servidor fica em RX por padrão (recebendo comandos)
    radio.setReceiveMode();
    radio.enable();
}

// -----------------------------------------------------------------------------
// Função principal
// -----------------------------------------------------------------------------
int main()
{
    char txbuf[TRANSFER_SIZE];
    char rxbuf[TRANSFER_SIZE];

    pc_printf("\r\n=== SERVIDOR - Motores + Encoders + Ultrassom ===\r\n");

    // Inicia timer de debounce dos encoders
    encDebounceTimer.start();

    // Configuração dos encoders
    encRight.mode(PullUp);
    encLeft.mode(PullUp);

    encRight.rise(&on_pulse_right);
    encLeft.rise(&on_pulse_left);

    ambos_stop();
    config_radio();

    // Configuração do HC-SR04
    trig = 0; // nível baixo inicialmente
    echo.rise(&echo_rise);
    echo.fall(&echo_fall);
    echoTimer.start();

    // Timer para telemetria e ultrassom
    Timer telemTimer;
    telemTimer.start();

    Timer ultrassomTimer;
    ultrassomTimer.start();

    sysTimer.start();

    const int TELEMETRY_PERIOD_MS = 100; // período de envio de telemetria
    const int ULTRASSOM_PERIOD_MS = 60;  // período de disparo do ultrassom

    int32_t cR = 0, cL = 0;
    float dR = 0.0f, dL = 0.0f;
    float distObs_cm = 0.0f;

    while (true)
    {
        // -----------------------------------------------------------------
        // 1) Atualiza medição do ultrassom (disparo + cálculo)
        // -----------------------------------------------------------------
        if (ultrassomTimer.read_ms() >= ULTRASSOM_PERIOD_MS)
        {
            ultrassomTimer.reset();
            send_trigger_pulse();
        }

        if (new_measure)
        {
            new_measure = false;
            float duration_us = (float)echo_pulse_us;
            // dist (cm) = (tempo_us * 0.0343) / 2 = tempo_us * 0.01715
            distObs_cm = duration_us * 0.01715f;
            last_dist_cm = distObs_cm;
        }

        // -----------------------------------------------------------------
        // 2) Recebe comandos do cliente (posição em L)
        // -----------------------------------------------------------------
        if (radio.readable())
        {
            int n = radio.read(NRF24L01P_PIPE_P0, rxbuf, TRANSFER_SIZE);
            if (n > 0)
            {
                char cmd = rxbuf[0];

                if (cmd == 'P' && n >= 5)
                {
                    int16_t x_cm = (int16_t)(((uint8_t)rxbuf[1] << 8) | (uint8_t)rxbuf[2]);
                    int16_t y_cm = (int16_t)(((uint8_t)rxbuf[3] << 8) | (uint8_t)rxbuf[4]);

                    targetX_cm = (float)x_cm;
                    targetY_cm = (float)y_cm;

                    pc_printf("[CMD] Novo alvo X=%d cm, Y=%d cm\r\n", x_cm, y_cm);

                    reset_encoders();
                    obstacle_active = false;
                    state = STATE_MOVE_X;
                }

                ledRx = !ledRx;
            }
        }

        // -----------------------------------------------------------------
        // 3) Lógica de movimento (FSM)
        // -----------------------------------------------------------------
        cR = pulse_right;
        cL = pulse_left;
        dR = cR * CM_PER_PULSE_RIGHT;
        dL = cL * CM_PER_PULSE_LEFT;
        float dAvg = 0.5f * (dR + dL);

        switch (state)
        {
        case STATE_IDLE:
            ambos_stop();
            break;

        case STATE_MOVE_X:
            // Se obstáculo estiver presente na primeira perna do L
            if (!obstacle_active && distObs_cm > 0.0f && distObs_cm < OBSTACLE_THRESHOLD_CM)
            {
                obstacle_active = true;

                // Calcula quanto já foi percorrido em X
                float dX = dAvg;
                obs_residualX = targetX_cm - dX;
                if (obs_residualX < 0.0f)
                    obs_residualX = 0.0f;

                ambos_stop();

                char distStr[16];
                float_to_str(distStr, distObs_cm);

                pc_printf("[OBS] Obstaculo detectado a %s cm. dX=%.2f, restante=%.2f cm\r\n",
                          distStr, dX, obs_residualX);

                reset_encoders();
                turn_start_ms = sysTimer.read_ms();
                state = STATE_OBS_TURN_RIGHT;
            }
            else
            {
                // Movimento normal na perna X
                if (dAvg >= targetX_cm)
                {
                    ambos_stop();
                    pc_printf("[MOVE] Perna X concluida (%.2f cm)\r\n", targetX_cm);
                    reset_encoders();
                    turn_start_ms = sysTimer.read_ms();
                    state = STATE_TURN_90;
                }
                else
                {
                    ambos_forward();
                }
            }
            break;

        case STATE_TURN_90:
        {
            int elapsed = sysTimer.read_ms() - turn_start_ms;
            if (elapsed >= TURN_TIME_MS)
            {
                ambos_stop();
                pc_printf("[MOVE] Curva 90° concluida.\r\n");
                reset_encoders();
                state = STATE_MOVE_Y;
            }
            else
            {
                // girar 90° para a direita (direita para frente, esquerda ré)
                motorA_backward();
                motorB_forward();
            }
            break;
        }

        case STATE_MOVE_Y:
            if (dAvg >= targetY_cm)
            {
                ambos_stop();
                pc_printf("[MOVE] Perna Y concluida (%.2f cm). Trajetoria em L finalizada.\r\n",
                          targetY_cm);
                reset_encoders();
                state = STATE_IDLE;
            }
            else
            {
                ambos_forward();
            }
            break;

        // --------- Estados de desvio de obstáculo ---------
        case STATE_OBS_TURN_RIGHT:
        {
            int elapsed = sysTimer.read_ms() - turn_start_ms;
            if (elapsed >= TURN_TIME_MS)
            {
                ambos_stop();
                pc_printf("[OBS] Curva 90° (direita) concluida.\r\n");
                reset_encoders();
                state = STATE_OBS_MOVE1;
            }
            else
            {
                // gira 90° à direita
                motorA_backward();
                motorB_forward();
            }
            break;
        }

        case STATE_OBS_MOVE1:
            // anda em linha reta com base no que faltava em X
            if (dAvg >= obs_residualX)
            {
                ambos_stop();
                pc_printf("[OBS] MOVE1 (%.2f cm) concluido. Preparando curva à esquerda.\r\n",
                          obs_residualX);
                reset_encoders();
                turn_start_ms = sysTimer.read_ms();
                state = STATE_OBS_TURN_LEFT;
            }
            else
            {
                ambos_forward();
            }
            break;

        case STATE_OBS_TURN_LEFT:
        {
            int elapsed = sysTimer.read_ms() - turn_start_ms;
            if (elapsed >= TURN_TIME_MS)
            {
                ambos_stop();
                pc_printf("[OBS] Curva 90° (esquerda) concluida.\r\n");
                reset_encoders();
                state = STATE_OBS_MOVE2;
            }
            else
            {
                // gira 90° à esquerda (direita ré, esquerda frente)
                motorA_forward();
                motorB_backward();
            }
            break;
        }

        case STATE_OBS_MOVE2:
            // anda em linha reta a perna Y original
            if (dAvg >= targetY_cm)
            {
                ambos_stop();
                pc_printf("[OBS] MOVE2 (Y=%.2f cm) concluido. Fim da trajetória.\r\n",
                          targetY_cm);
                reset_encoders();
                obstacle_active = false;
                state = STATE_IDLE;
            }
            else
            {
                ambos_forward();
            }
            break;
        }

        // -----------------------------------------------------------------
        // 4) Telemetria: distâncias das duas rodas + distância do ultrassom
        // -----------------------------------------------------------------
        if (telemTimer.read_ms() >= TELEMETRY_PERIOD_MS)
        {
            telemTimer.reset();

            cR = pulse_right;
            cL = pulse_left;
            dR = cR * CM_PER_PULSE_RIGHT;
            dL = cL * CM_PER_PULSE_LEFT;

            char sR[16], sL[16], sObs[16];
            float_to_str(sR, dR);
            float_to_str(sL, dL);
            float_to_str(sObs, distObs_cm);

            // --- Monta payload binário para o cliente ---
            for (int i = 0; i < TRANSFER_SIZE; i++)
                txbuf[i] = 0;

            // Formato:
            //  [0]      = 'T'
            //  [1-2]    = cR (int16)
            //  [3-4]    = cL (int16)
            //  [5-6]    = dR_cm * 100 (int16)
            //  [7-8]    = dL_cm * 100 (int16)
            //  [9]      = state (int8)
            //  [10-11]  = distObs_cm * 100 (int16)

            int16_t cR16 = (cR > 32767) ? 32767 : (cR < -32768 ? -32768 : (int16_t)cR);
            int16_t cL16 = (cL > 32767) ? 32767 : (cL < -32768 ? -32768 : (int16_t)cL);

            int16_t dR_i = (int16_t)(dR * 100.0f);
            int16_t dL_i = (int16_t)(dL * 100.0f);
            int16_t obs_i = (int16_t)(distObs_cm * 100.0f);

            txbuf[0] = 'T';
            put_i16(txbuf, 1, cR16);
            put_i16(txbuf, 3, cL16);
            put_i16(txbuf, 5, dR_i);
            put_i16(txbuf, 7, dL_i);
            txbuf[9] = (char)state;
            put_i16(txbuf, 10, obs_i);

            radio.setTransmitMode();
            radio.enable();
            radio.write(NRF24L01P_PIPE_P0, txbuf, TRANSFER_SIZE);

            // Debug local no servidor (mantido)
            pc_printf("[DIST] R_p=%ld  L_p=%ld  R=%s cm  L=%s cm  estado=%d  Obs=%s cm\r\n",
                      (long)cR, (long)cL, sR, sL, (int)state, sObs);

            radio.setReceiveMode();
            radio.enable();
        }

        ThisThread::sleep_for(5ms);
    }
}
