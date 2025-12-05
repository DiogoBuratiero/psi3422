#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>

static const int TRANSFER_SIZE = 20;             // telemetria + comandos
static const long long RF_ADDR = 0xE7E7E7E7E7LL; // referência

BufferedSerial pc(USBTX, USBRX, 115200);
nRF24L01P radio(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13); // mosi, miso, sck, csn, ce, irq
DigitalOut ledTx(LED1);

// printf simplificado
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

void config_radio()
{
    radio.powerUp();
    thread_sleep_for(10);

    radio.setRfFrequency(2476); // 2.476 GHz
    radio.setAirDataRate(1000); // 1 Mbps
    radio.setRfOutputPower(0);  // 0 dBm

    const long long TX_ADDR = 0xA1A1A1A1A1LL; // cliente -> servidor (comandos/posições)
    const long long RX_ADDR = 0xB1B1B1B1B1LL; // servidor -> cliente (telemetria)

    radio.setTxAddress(TX_ADDR);
    radio.setRxAddress(RX_ADDR);

    radio.setTransferSize(TRANSFER_SIZE);

    // Cliente fica em RX por padrão (recebendo telemetria)
    radio.setReceiveMode();
    radio.enable();
}

// envia um comando de posição X,Y em cm ao servidor
void send_position(int16_t x_cm, int16_t y_cm)
{
    char tx[TRANSFER_SIZE];

    // Payload: 'P' + X_hi + X_lo + Y_hi + Y_lo + padding
    for (int i = 0; i < TRANSFER_SIZE; i++)
        tx[i] = 0;

    tx[0] = 'P';
    tx[1] = (char)((x_cm >> 8) & 0xFF);
    tx[2] = (char)(x_cm & 0xFF);
    tx[3] = (char)((y_cm >> 8) & 0xFF);
    tx[4] = (char)(y_cm & 0xFF);

    radio.setTransmitMode();
    radio.enable();
    radio.write(NRF24L01P_PIPE_P0, tx, TRANSFER_SIZE);
    ledTx = !ledTx;

    // volta para RX de telemetria
    radio.setReceiveMode();
    radio.enable();
}

int main()
{
    char rx[TRANSFER_SIZE];
    char line[32];
    int idx = 0;

    pc_printf("\r\n=== CLIENTE - Coordenadas em L + Telemetria ===\r\n");
    pc_printf("Digite coordenadas alvo em cm como:\r\n");
    pc_printf("  X Y  (ex.: 30 40)\r\n");
    pc_printf("O carrinho fará uma trajetória em L até (X,Y).\r\n\r\n");

    config_radio();

    while (true)
    {
        // -------- 1) Leitura de linha do terminal (coordenadas) --------
        if (pc.readable())
        {
            char c;
            if (pc.read(&c, 1) == 1)
            {
                // eco
                pc.write(&c, 1);

                if (c == '\r' || c == '\n')
                {
                    // fim de linha → processar
                    line[idx] = '\0';
                    idx = 0;

                    int x, y;
                    if (sscanf(line, "%d %d", &x, &y) == 2)
                    {
                        if (x < -32768)
                            x = -32768;
                        if (x > 32767)
                            x = 32767;
                        if (y < -32768)
                            y = -32768;
                        if (y > 32767)
                            y = 32767;

                        pc_printf("\r\n[CMD] Enviando alvo X=%d cm, Y=%d cm\r\n", x, y);
                        send_position((int16_t)x, (int16_t)y);
                    }
                    else
                        pc_printf("\r\n[ERRO] Formato invalido. Use: X Y\r\n");
                }
                else
                    // acumula na linha
                    if (idx < (int)sizeof(line) - 1)
                        line[idx++] = c;
            }
        }

        // -------- 2) Receber telemetria do servidor --------
        if (radio.readable())
        {
            int n = radio.read(NRF24L01P_PIPE_P0, rx, TRANSFER_SIZE);
            if (n > 0)
            {
                char telem[TRANSFER_SIZE + 1];
                for (int i = 0; i < TRANSFER_SIZE; i++)
                    telem[i] = rx[i];
                telem[TRANSFER_SIZE] = '\0';

                pc_printf("\r\n[DIST] %s cm\r\n", telem);
            }
        }

        ThisThread::sleep_for(10ms);
    }
}
