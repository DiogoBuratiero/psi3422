#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>

#define TRANSFER_SIZE 1 // recebemos apenas 1 byte: o comando

// Serial para debug (pode ficar ligada ao PC também, se quiser ver mensagens)
BufferedSerial pc(USBTX, USBRX, 115200);

// nRF24L01+  (mosi, miso, sck, csn, ce, irq)
nRF24L01P my_nrf24l01p(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13);

// LED remoto que será controlado
DigitalOut led_remote(LED1);

// LED de indicação de recepção
DigitalOut led_rx(LED2);

void pc_printf(const char *fmt, ...)
{
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (len > 0)
    {
        if (len > (int)sizeof(buffer))
            len = sizeof(buffer);
        pc.write(buffer, len);
    }
}

int main()
{
    char rxData[TRANSFER_SIZE];

    pc_printf("\r\n=== Placa 2 (RX) – LED remoto ===\r\n");

    my_nrf24l01p.powerUp();
    thread_sleep_for(10);

    // Mesma configuração RF da placa 1
    my_nrf24l01p.setRfFrequency(2476); // 2476 MHz
    my_nrf24l01p.setAirDataRate(1000); // 1 Mbps
    my_nrf24l01p.setRfOutputPower(0);  // 0 dBm

    const long long TX_ADDR = 0xB1B1B1B1B1LL;
    const long long RX_ADDR = 0xA1A1A1A1A1LL;

    my_nrf24l01p.setTxAddress(TX_ADDR);
    my_nrf24l01p.setRxAddress(RX_ADDR);

    my_nrf24l01p.setTransferSize(TRANSFER_SIZE);
    my_nrf24l01p.setReceiveMode();
    my_nrf24l01p.enable();

    pc_printf("Rádio em modo RX, aguardando comandos...\r\n");
    pc_printf("Comandos válidos: 'L' (ligar LED), 'D' (desligar LED).\r\n\r\n");

    led_remote = 0; // começa desligado

    while (true)
    {
        if (my_nrf24l01p.readable())
        {
            int rxCnt = my_nrf24l01p.read(NRF24L01P_PIPE_P0, rxData, TRANSFER_SIZE);

            if (rxCnt > 0)
            {
                char cmd = rxData[0];
                pc_printf("[RX] Recebido comando '%c' (0x%02X)\r\n",
                          (cmd >= 32 && cmd <= 126) ? cmd : '.',
                          (unsigned char)cmd);

                if (cmd == 'L')
                {
                    led_remote = 0;
                    pc_printf(" -> LED remoto LIGADO\r\n");
                }
                else if (cmd == 'D')
                {
                    led_remote = 1;
                    pc_printf(" -> LED remoto DESLIGADO\r\n");
                }
                else
                {
                    pc_printf(" -> Comando desconhecido, ignorando.\r\n");
                }

                // Pisca o LED2 para indicar recepção
                // led_rx = !led_rx;
            }
        }
    }
}
