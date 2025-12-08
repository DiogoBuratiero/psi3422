#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>

static const int TRANSFER_SIZE = 20; // ex: "R:12.34 L:11.90"

BufferedSerial pc(USBTX, USBRX, 115200);
nRF24L01P radio(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13);
DigitalOut ledRx(LED2);

// Motores (L298N)
DigitalOut in1(PTB0); // Motor A (direita)
DigitalOut in2(PTB1);
DigitalOut in3(PTB2); // Motor B (esquerda)
DigitalOut in4(PTB3);

// Encoders
// Ajuste PTA1 / PTA2 para os pinos reais ligados aos sensores
InterruptIn encRight(PTA5); // encoder roda direita
InterruptIn encLeft(PTA1);  // encoder roda esquerda

volatile int32_t pulse_right = 0;
volatile int32_t pulse_left = 0;

// Calibração (se forem diferentes, separar constantes por lado)
static const float CM_PER_PULSE_RIGHT = 0.69f;
static const float CM_PER_PULSE_LEFT = 0.69f;

// -------- PRINTF ----------
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

// -------- FLOAT → STRING (sem usar %f em printf) ----------
void float_to_str(char *out, float v)
{
  int inteiro = (int)v;
  int frac = (int)((v - inteiro) * 100.0f);
  if (frac < 0)
    frac = -frac;
  snprintf(out, 16, "%d.%02d", inteiro, frac);
}

// -------- Motores ----------
 // direita
inline void motorA_forward()
{
  in1 = 0;
  in2 = 1;
}
inline void motorA_stop()
{
  in1 = 0;
  in2 = 0;
}

// esquerda
inline void motorB_forward()
{
  in3 = 1;
  in4 = 0;
}
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

  // Modo padrão: RX para receber comandos
  radio.setReceiveMode();
  radio.enable();
}

// ISR dos encoders
void on_pulse_right() { pulse_right++; }
void on_pulse_left() { pulse_left++; }

int main()
{
  char txbuf[TRANSFER_SIZE];

  pc_printf("\r\n=== Placa 2 (SERVIDOR – Motores + 2 Encoders) ===\r\n");

  // Configuração dos encoders
  encRight.mode(PullUp);
  encLeft.mode(PullUp);

  encRight.rise(&on_pulse_right);
  encRight.fall(&on_pulse_right); // remova se não quiser contar ambas as bordas

  encLeft.rise(&on_pulse_left);
  encLeft.fall(&on_pulse_left); // idem

  ambos_stop();
  config_radio();

  Timer t;
  t.start();
  const int TELEMETRY_PERIOD_MS = 100;

  while (true)
  {
    // -------- 1) Receber comandos do cliente --------
    if (radio.readable())
    {
      char cmdBuf[TRANSFER_SIZE];
      int n = radio.read(NRF24L01P_PIPE_P0, cmdBuf, TRANSFER_SIZE);
      if (n > 0)
      {
        char cmd = cmdBuf[0];

        switch (cmd)
        {
        case 'D': // motor direito
          motorA_forward();
          motorB_stop();
          pc_printf("[CMD] D -> motor direito\r\n");
          break;

        case 'E': // motor esquerdo
          motorA_stop();
          motorB_forward();
          pc_printf("[CMD] E -> motor esquerdo\r\n");
          break;

        case 'B': // ambos
          motorA_forward();
          motorB_forward();
          pc_printf("[CMD] B -> ambos motores\r\n");
          break;

        case 'S': // STOP explícito
        default:
          ambos_stop();
          pc_printf("[CMD] STOP\r\n");
          break;
        }

        ledRx = !ledRx;
      }
    }

    // -------- 2) Telemetria: distâncias direita/esquerda --------
    if (t.read_ms() >= TELEMETRY_PERIOD_MS)
    {
      t.reset();

      // Cópias locais dos contadores (variáveis voláteis)
      int32_t cR = pulse_right;
      int32_t cL = pulse_left;

      float dR = cR * CM_PER_PULSE_RIGHT;
      float dL = cL * CM_PER_PULSE_LEFT;

      char sR[16], sL[16];
      float_to_str(sR, dR);
      float_to_str(sL, dL);

      // Monta string compacta: "R:xx.xx L:yy.yy"
      for (int i = 0; i < TRANSFER_SIZE; i++)
        txbuf[i] = 0;

      snprintf(txbuf, TRANSFER_SIZE, "R:%s L:%s", sR, sL);

      // Envia telemetria ao cliente
      radio.setTransmitMode();
      radio.enable();
      radio.write(NRF24L01P_PIPE_P0, txbuf, TRANSFER_SIZE);

      // Debug local
      pc_printf("[DIST] R_p=%ld  L_p=%ld  R=%s cm  L=%s cm\r\n",
                (long)cR, (long)cL, sR, sL);

      // Volta para RX de comandos
      radio.setReceiveMode();
      radio.enable();
    }

    ThisThread::sleep_for(5ms);
  }
}
