#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>

static const int TRANSFER_SIZE = 1;                // 1 byte por comando
static const long long RF_ADDR = 0xE7E7E7E7E7LL;  // endereço comum

BufferedSerial pc(USBTX, USBRX, 115200);
nRF24L01P radio(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13); // mosi, miso, sck, csn, ce, irq
DigitalOut ledTx(LED1);

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

void config_radio() {
    radio.powerUp();
    thread_sleep_for(10);

    // Configuração RF (deve ser idêntica na outra placa)
    radio.setRfFrequency(2476); // 2476 MHz
    radio.setAirDataRate(1000); // 1000 kbps = 1 Mbps
    radio.setRfOutputPower(0);  // 0 dBm

    const long long TX_ADDR = 0xA1A1A1A1A1LL;
    const long long RX_ADDR = 0xB1B1B1B1B1LL;

    radio.setTxAddress(TX_ADDR);
    radio.setRxAddress(RX_ADDR);

    radio.setTransferSize(TRANSFER_SIZE);
    radio.setTransmitMode();
    radio.enable();
}

int main() {
    char tx[TRANSFER_SIZE];

    pc_printf("\r\n=== Placa 1 (TX) – Controle dos motores ===\r\n");
    pc_printf("Comandos:\r\n");
    pc_printf("  D = motor direito\r\n");
    pc_printf("  E = motor esquerdo\r\n");
    pc_printf("  B = ambos motores\r\n\r\n");

    config_radio();

    while (true) {
        if (!pc.readable()) continue;

        char c;
        if (pc.read(&c, 1) != 1) continue;

        // eco simples no terminal
        pc.write(&c, 1);

        if (c == 'D' || c == 'E' || c == 'B') {
            tx[0] = c;
            radio.write(NRF24L01P_PIPE_P0, tx, TRANSFER_SIZE);
            ledTx = !ledTx;
        } else {
            pc_printf("\r\nUse apenas D, E ou B.\r\n");
        }
    }
}
