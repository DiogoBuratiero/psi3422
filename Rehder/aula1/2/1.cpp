#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>

#define TRANSFER_SIZE 1 // vamos enviar apenas 1 byte: o comando

// Serial com o PC
BufferedSerial pc(USBTX, USBRX, 115200);

// nRF24L01+  (mosi, miso, sck, csn, ce, irq)
nRF24L01P my_nrf24l01p(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13);

// LED local apenas como indicação de transmissão
DigitalOut led_tx(LED1);

// Endereço RF (mesmo em ambas as placas)
static const long long RF_ADDR = 0xE7E7E7E7E7LL;

// Pequeno printf sobre BufferedSerial
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
    char txData[TRANSFER_SIZE];

    pc_printf("\r\n=== Placa 1 (TX) – Controle remoto do LED ===\r\n");

    // Liga o rádio
    my_nrf24l01p.powerUp();
    thread_sleep_for(10);

    // Configuração RF (deve ser idêntica na outra placa)
    my_nrf24l01p.setRfFrequency(2476); // 2476 MHz
    my_nrf24l01p.setAirDataRate(1000); // 1000 kbps = 1 Mbps
    my_nrf24l01p.setRfOutputPower(0);  // 0 dBm

    // Mesmo endereço para TX e RX (pipe 0) nas duas placas
    my_nrf24l01p.setTxAddress(RF_ADDR);
    my_nrf24l01p.setRxAddress(RF_ADDR, NRF24L01P_PIPE_P0);

    my_nrf24l01p.setTransferSize(TRANSFER_SIZE);
    my_nrf24l01p.setTransmitMode();
    my_nrf24l01p.enable();

    pc_printf("Configuração concluída.\r\n");
    pc_printf("Digite 'L' para ligar o LED remoto, 'D' para desligar.\r\n\r\n");

    while (true)
    {
        if (pc.readable())
        {
            char c;
            if (pc.read(&c, 1) == 1)
            {

                // Eco no terminal
                pc.write(&c, 1);

                if (c == 'L' || c == 'D')
                {
                    txData[0] = c;

                    pc_printf("\r\n[TX] Enviando comando '%c'...\r\n", c);

                    my_nrf24l01p.write(NRF24L01P_PIPE_P0, txData, TRANSFER_SIZE);

                    // pisca LED local para indicar envio
                    led_tx = !led_tx;
                }
                else
                {
                    pc_printf("\r\n[WARN] Comando inválido. Use apenas 'L' ou 'D'.\r\n");
                }
            }
        }
    }
}
