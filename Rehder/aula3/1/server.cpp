#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>

static const int TRANSFER_SIZE = 8;              // ex: "12.34" + padding
static const long long RF_ADDR = 0xE7E7E7E7E7LL; // referência

BufferedSerial pc(USBTX, USBRX, 115200);
nRF24L01P radio(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13);
DigitalOut ledRx(LED2);

// Motores (L298N)
DigitalOut in1(PTB0); // Motor A (direita)
DigitalOut in2(PTB1);
DigitalOut in3(PTB2); // Motor B (esquerda)
DigitalOut in4(PTB3);

// Encoder
InterruptIn encoder(PTA1);
// InterruptIn encoder(PTA2); // TODO: adicionar segundo encoder
volatile int32_t pulse_count = 0;

// Calibração do encoder (ajuste a partir da medição real)
static const float CM_PER_PULSE = 0.69f; // cm por pulso

// -------- PRINTF ----------
void pc_printf(const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) {
    if (n > (int)sizeof(buf))
      n = sizeof(buf);
    pc.write(buf, n);
  }
}

// -------- FLOAT → STRING (sem usar %f em printf) ----------
void float_to_str(char *out, float v) {
  int inteiro = (int)v;
  int frac = (int)((v - inteiro) * 100.0f);
  if (frac < 0)
    frac = -frac;
  snprintf(out, 16, "%d.%02d", inteiro, frac);
}

// -------- Motores ----------
inline void motorA_forward() {
  in1 = 0;
  in2 = 1;
}
inline void motorA_stop() {
  in1 = 0;
  in2 = 0;
}

inline void motorB_forward() {
  in3 = 1;
  in4 = 0;
}
inline void motorB_stop() {
  in3 = 0;
  in4 = 0;
}

inline void ambos_stop() {
  motorA_stop();
  motorB_stop();
}

void config_radio() {
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

// ISR do encoder: conta pulsos
void on_pulse() { pulse_count++; }

int main() {
  char txbuf[TRANSFER_SIZE];

  pc_printf("\r\n=== Placa 2 (SERVIDOR – Motores + Encoder) ===\r\n");

  // Encoder: pull-up e interrupções nas bordas
  encoder.mode(PullUp);
  encoder.rise(&on_pulse);
  encoder.fall(&on_pulse); // se não quiser dobrar resolução, pode remover

  ambos_stop();
  config_radio();

  Timer t;
  t.start();
  const int TELEMETRY_PERIOD_MS = 100;

  while (true) {

    // -------- 1) Receber comandos do cliente --------
    if (radio.readable()) {
      char cmdBuf[TRANSFER_SIZE];
      int n = radio.read(NRF24L01P_PIPE_P0, cmdBuf, TRANSFER_SIZE);
      if (n > 0) {
        char cmd = cmdBuf[0];

        switch (cmd) {
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

    // -------- 2) Telemetria: distância (cm) enviada ao cliente --------
    if (t.read_ms() >= TELEMETRY_PERIOD_MS) {
      t.reset();

      int32_t count = pulse_count;
      float dist_cm = count * CM_PER_PULSE;

      // Converte para string "xx.xx"
      char dist_str[16];
      float_to_str(dist_str, dist_cm);

      // Preenche payload fixo com a string
      for (int i = 0; i < TRANSFER_SIZE; i++) {
        txbuf[i] = 0;
      }
      // cópia segura com limite TRANSFER_SIZE
      snprintf(txbuf, TRANSFER_SIZE, "%s", dist_str);

      // Muda temporariamente para TX, envia, depois volta para RX
      radio.setTransmitMode();
      radio.enable();
      radio.write(NRF24L01P_PIPE_P0, txbuf, TRANSFER_SIZE);

      // também imprime localmente para debug
      pc_printf("[DIST] pulses=%ld  dist=%s cm\r\n", (long)count, dist_str);

      radio.setReceiveMode();
      radio.enable();
    }

    ThisThread::sleep_for(5ms);
  }
}
