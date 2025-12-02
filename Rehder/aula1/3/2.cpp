#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>

static const int TRANSFER_SIZE = 1;
static const long long RF_ADDR = 0xE7E7E7E7E7LL;

BufferedSerial pc(USBTX, USBRX, 115200);
nRF24L01P radio(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13); // mosi, miso, sck, csn, ce, irq

// L298N – direção dos motores
DigitalOut in1(PTB0);   // Motor A (direita)
DigitalOut in2(PTB1);
DigitalOut in3(PTB2);   // Motor B (esquerda)
DigitalOut in4(PTB3);

DigitalOut ledRx(LED2); // indicação de recepção

void pc_printf(const char *fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if (n > (int)sizeof(buf)) n = sizeof(buf);
        pc.write(buf, n);
    }
}

// Motor A = direita
inline void motorA_forward() { in1 = 0; in2 = 1; }
inline void motorA_stop()    { in1 = 0; in2 = 0; }

// Motor B = esquerda
inline void motorB_forward() { in3 = 1; in4 = 0; }
inline void motorB_stop()    { in3 = 0; in4 = 0; }

inline void ambos_stop() {
    motorA_stop();
    motorB_stop();
}

void config_radio() {
    radio.powerUp();
    thread_sleep_for(10);

    // Configuração RF (deve ser idêntica na outra placa)
    radio.setRfFrequency(2476); // 2476 MHz
    radio.setAirDataRate(1000); // 1000 kbps = 1 Mbps
    radio.setRfOutputPower(0);  // 0 dBm

    const long long TX_ADDR = 0xB1B1B1B1B1LL;
    const long long RX_ADDR = 0xA1A1A1A1A1LL;

    radio.setTxAddress(TX_ADDR);
    radio.setRxAddress(RX_ADDR);

    radio.setTransferSize(TRANSFER_SIZE);
    radio.setReceiveMode();
    radio.enable();
}

int main() {
    char rx[TRANSFER_SIZE];

    pc_printf("\r\n=== Placa 2 (RX) – Controle de motores ===\r\n");
    pc_printf("Aguardando comandos D, E ou B...\r\n\r\n");

    ambos_stop();
    config_radio();

    while (true) {
        if (!radio.readable()) continue;

        int n = radio.read(NRF24L01P_PIPE_P0, rx, TRANSFER_SIZE);

        if (n <= 0) continue;
        char cmd = rx[0];

        switch (cmd) {
            case 'D': // motor direito
                motorA_forward();
                motorB_stop();
                pc_printf("[RX] D -> motor direito\r\n");
                break;

            case 'E': // motor esquerdo
                motorA_stop();
                motorB_forward();
                pc_printf("[RX] E -> motor esquerdo\r\n");
                break;

            case 'B': // ambos
                motorA_forward();
                motorB_forward();
                pc_printf("[RX] B -> ambos motores\r\n");
                break;

            default:  // qualquer outro comando: parar
                ambos_stop();
                pc_printf("[RX] comando invalido -> parar motores\r\n");
                break;
        }

        ledRx = !ledRx;
    }
}
