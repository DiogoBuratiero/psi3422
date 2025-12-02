#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>

// -------------------- CONFIG --------------------
#define TRANSFER_SIZE 4

// Initialize BufferedSerial object for PC communication
BufferedSerial pc(USBTX, USBRX, 115200); // baud 115200

// nRF24L01+ module setup: mosi, miso, sck, csn, ce, irq
nRF24L01P my_nrf24l01p(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13);

// LED indicators
DigitalOut myled1(LED1);
DigitalOut myled2(LED2);

// -------------------- HELPER: printf via BufferedSerial --------------------
void pc_printf(const char *fmt, ...) {
    char buffer[128];

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len > 0) {
        if (len > (int)sizeof(buffer)) {
            len = sizeof(buffer);
        }
        pc.write(buffer, len);
    }
}

int main() {
    char txData[TRANSFER_SIZE] = {0};
    char rxData[TRANSFER_SIZE] = {0};
    int txDataCnt = 0;
    int rxDataCnt = 0;

    pc_printf("\r\n=== Inicializando nRF24L01+ ===\r\n");

    // Power up do rádio
    pc_printf("Powering up the nRF24L01+...\r\n");
    my_nrf24l01p.powerUp();

    // Pequeno delay para garantir que o rádio acordou
    thread_sleep_for(10);

    // === CONFIGURAR FREQUÊNCIA AQUI ===
    my_nrf24l01p.setRfFrequency(2476);   // 2.476 GHz (canal 76, por exemplo)

    // Mostrar configuração atual do rádio
    pc_printf("nRF24L01+ Frequency    : %d MHz\r\n",  my_nrf24l01p.getRfFrequency());
    pc_printf("nRF24L01+ Output power : %d dBm\r\n",  my_nrf24l01p.getRfOutputPower());
    pc_printf("nRF24L01+ Data Rate    : %d kbps\r\n", my_nrf24l01p.getAirDataRate());
    pc_printf("nRF24L01+ TX Address   : 0x%010llX\r\n",
              (long long)my_nrf24l01p.getTxAddress());
    pc_printf("nRF24L01+ RX Address   : 0x%010llX\r\n",
              (long long)my_nrf24l01p.getRxAddress());

    pc_printf("\r\nDigite caracteres para testar as transferências:\r\n");
    pc_printf("  (os envios são agrupados em %d caracteres)\r\n\r\n", TRANSFER_SIZE);

    my_nrf24l01p.setTransferSize(TRANSFER_SIZE);
    my_nrf24l01p.setReceiveMode();
    my_nrf24l01p.enable();

    pc_printf("Rádio configurado em modo RX, aguardando...\r\n");

    while (1) {
        // ---------- TX: ler do serial do PC e enviar via rádio ----------
        if (pc.readable()) {
            char c;
            if (pc.read(&c, 1) == 1) {
                txData[txDataCnt++] = c;
                pc_printf("[DEBUG] Digitado: '%c' (0x%02X), txDataCnt=%d\r\n",
                          (c >= 32 && c <= 126) ? c : '.',
                          (unsigned char)c,
                          txDataCnt);
            }

            // Buffer cheio -> enviar
            if (txDataCnt >= (int)sizeof(txData)) {
                pc_printf("[TX] Enviando %d bytes via nRF24L01+: ", txDataCnt);
                for (int i = 0; i < txDataCnt; i++) {
                    pc_printf("0x%02X ", (unsigned char)txData[i]);
                }
                pc_printf("\r\n");

                my_nrf24l01p.write(NRF24L01P_PIPE_P0, txData, txDataCnt);

                myled1 = !myled1;
                txDataCnt = 0;
            }
        }

        // ---------- RX: dados vindo do rádio ----------
        if (my_nrf24l01p.readable()) {
            rxDataCnt = my_nrf24l01p.read(NRF24L01P_PIPE_P0, rxData, sizeof(rxData));

            pc_printf("[RX] Recebidos %d bytes do nRF24L01+: ", rxDataCnt);
            for (int i = 0; i < rxDataCnt; i++) {
                pc_printf("0x%02X ", (unsigned char)rxData[i]);
            }
            pc_printf("\r\n");

            // Também jogar direto pro terminal (pode ser binário)
            pc.write(rxData, rxDataCnt);

            myled2 = !myled2;
        }
    }
}
