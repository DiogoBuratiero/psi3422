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

// Encoders (ajuste os pinos conforme seu hardware real)
InterruptIn encRight(PTA1); // encoder roda direita
InterruptIn encLeft(PTA2);  // encoder roda esquerda

volatile int32_t pulse_right = 0;
volatile int32_t pulse_left = 0;

// Calibrações (ajustar na prática)
static const float CM_PER_PULSE_RIGHT = 0.69f;
static const float CM_PER_PULSE_LEFT = 0.69f;

// Tempo de curva de 90° (ms) — calibrado empiricamente
static const int TURN_TIME_MS = 550;

// Estados da trajetória
enum MoveState
{
  STATE_IDLE = 0,
  STATE_MOVE_X,
  STATE_TURN_90,
  STATE_MOVE_Y
};

MoveState state = STATE_IDLE;
float targetX_cm = 0.0f;
float targetY_cm = 0.0f;

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

void on_pulse_right() { pulse_right++; }
void on_pulse_left() { pulse_left++; }

// -----------------------------------------------------------------------------
// Rádio
// -----------------------------------------------------------------------------
void config_radio()
{
  radio.powerUp();
  thread_sleep_for(10);

  radio.setRfFrequency(2476);
  radio.setAirDataRate(1000);
  radio.setRfOutputPower(0);

  // Servidor RX <- Cliente TX
  const long long RX_ADDR = 0xA1A1A1A1A1LL;
  // Servidor TX -> Cliente RX
  const long long TX_ADDR = 0xB1B1B1B1B1LL;

  radio.setTxAddress(TX_ADDR);
  radio.setRxAddress(RX_ADDR);

  radio.setTransferSize(TRANSFER_SIZE);

  radio.setReceiveMode();
  radio.enable();
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------
int main()
{
  char txbuf[TRANSFER_SIZE];

  pc_printf("\r\n=== SERVIDOR – Trajetoria em L + 2 Encoders (curva por tempo) ===\r\n");

  // Configuração dos encoders
  encRight.mode(PullUp);
  encLeft.mode(PullUp);

  encRight.rise(&on_pulse_right);
  encRight.fall(&on_pulse_right);
  encLeft.rise(&on_pulse_left);
  encLeft.fall(&on_pulse_left);

  ambos_stop();
  config_radio();

  // Timer de telemetria
  Timer telemTimer;
  telemTimer.start();
  const int TELEMETRY_PERIOD_MS = 100;

  // Timer global de movimento
  sysTimer.start();

  while (true)
  {
    // ---------------------------------------------------------------------
    // 1) Receber comando de coordenada do cliente
    // ---------------------------------------------------------------------
    if (radio.readable())
    {
      char rxbuf[TRANSFER_SIZE];
      int n = radio.read(NRF24L01P_PIPE_P0, rxbuf, TRANSFER_SIZE);
      if (n > 0)
      {
        char type = rxbuf[0];

        if (type == 'P')
        {
          int16_t x = (int16_t)((rxbuf[1] << 8) | (uint8_t)rxbuf[2]);
          int16_t y = (int16_t)((rxbuf[3] << 8) | (uint8_t)rxbuf[4]);

          targetX_cm = (float)x;
          targetY_cm = (float)y;

          char sx[16], sy[16];
          float_to_str(sx, targetX_cm);
          float_to_str(sy, targetY_cm);

          pc_printf("[CMD] Nova coordenada alvo: X=%s cm, Y=%s cm\r\n", sx, sy);

          reset_encoders();
          state = STATE_MOVE_X;
        }

        ledRx = !ledRx;
      }
    }

    // ---------------------------------------------------------------------
    // 2) Máquina de estados da trajetória em L
    // ---------------------------------------------------------------------
    // Leituras atuais dos encoders
    int32_t cR = pulse_right;
    int32_t cL = pulse_left;

    float dR = cR * CM_PER_PULSE_RIGHT;
    float dL = cL * CM_PER_PULSE_LEFT;
    float dAvg = 0.5f * (dR + dL);

    switch (state)
    {
    case STATE_IDLE:
      ambos_stop();
      break;

    case STATE_MOVE_X:
      // anda em linha reta até atingir X cm
      if (dAvg >= targetX_cm)
      {
        ambos_stop();
        pc_printf("[MOVE_X] alvo X atingido, iniciando curva 90°\r\n");
        reset_encoders();

        // marcar instante de início da curva
        turn_start_ms = sysTimer.read_ms();
        state = STATE_TURN_90;
      }
      else
      {
        motorA_forward();
        motorB_forward();
      }
      break;

    case STATE_TURN_90:
    {
      // curva de 90° baseada em tempo
      int now_ms = sysTimer.read_ms();
      int elapsed = now_ms - turn_start_ms;

      if (elapsed >= TURN_TIME_MS)
      {
        ambos_stop();
        pc_printf("[TURN] Curva 90° concluida (t=%d ms)\r\n", elapsed);
        reset_encoders();
        state = STATE_MOVE_Y;
      }
      else
      {
        // girar em torno do próprio eixo: direita frente, esquerda ré
        motorA_forward();
        motorB_backward();
      }
      break;
    }

    case STATE_MOVE_Y:
      // anda em linha reta até atingir Y cm
      if (dAvg >= targetY_cm)
      {
        ambos_stop();
        pc_printf("[MOVE_Y] alvo Y atingido, voltando a IDLE\r\n");
        reset_encoders();
        state = STATE_IDLE;
      }
      else
      {
        motorA_forward();
        motorB_forward();
      }
      break;
    }

    // ---------------------------------------------------------------------
    // 3) Telemetria: distâncias das duas rodas
    // ---------------------------------------------------------------------
    if (telemTimer.read_ms() >= TELEMETRY_PERIOD_MS)
    {
      telemTimer.reset();

      cR = pulse_right;
      cL = pulse_left;
      dR = cR * CM_PER_PULSE_RIGHT;
      dL = cL * CM_PER_PULSE_LEFT;

      char sR[16], sL[16];
      float_to_str(sR, dR);
      float_to_str(sL, dL);

      for (int i = 0; i < TRANSFER_SIZE; i++)
        txbuf[i] = 0;

      // "R:xx.xx L:yy.yy"
      snprintf(txbuf, TRANSFER_SIZE, "R:%s L:%s", sR, sL);

      radio.setTransmitMode();
      radio.enable();
      radio.write(NRF24L01P_PIPE_P0, txbuf, TRANSFER_SIZE);

      pc_printf("[DIST] R_p=%ld  L_p=%ld  R=%s cm  L=%s cm  estado=%d\r\n",
                (long)cR, (long)cL, sR, sL, (int)state);

      radio.setReceiveMode();
      radio.enable();
    }

    ThisThread::sleep_for(5ms);
  }
}
