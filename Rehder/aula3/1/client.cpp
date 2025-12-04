#include "mbed.h"
#include "nRF24L01P.h"
#include <cstdarg>

static const int TRANSFER_SIZE = 8;              // bytes de telemetria (ex: "12.34")
static const long long RF_ADDR = 0xE7E7E7E7E7LL; // não usamos diretamente, só para referência

BufferedSerial pc(USBTX, USBRX, 115200);
nRF24L01P radio(PTD2, PTD3, PTC5, PTD0, PTD5, PTA13); // mosi, miso, sck, csn, ce, irq
DigitalOut ledTx(LED1);

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

    // Configuração RF (mesmo canal, taxa e potência em ambas as placas)
    radio.setRfFrequency(2476); // 2.476 MHz
    radio.setAirDataRate(1000); // 1000 kbps = 1 Mbps
    radio.setRfOutputPower(0);  // 0 dBm

    // Endereços:
    // Cliente TX -> Servidor RX
    const long long TX_ADDR = 0xA1A1A1A1A1LL; // comandos indo para o carrinho
    // Servidor TX -> Cliente RX
    const long long RX_ADDR = 0xB1B1B1B1B1LL; // telemetria vindo do carrinho

    radio.setTxAddress(TX_ADDR);
    radio.setRxAddress(RX_ADDR); // pipe 0 por padrão

    radio.setTransferSize(TRANSFER_SIZE);

    // Por padrão, o cliente fica em modo RX, recebendo distância
    radio.setReceiveMode();
    radio.enable();
}

int main()
{
    char tx[TRANSFER_SIZE];
    char rx[TRANSFER_SIZE];

    pc_printf("\r\n=== Placa 1 (CLIENTE) – Controle + Telemetria ===\r\n");
    pc_printf("Comandos enviados ao carrinho:\r\n");
    pc_printf("  D = motor direito\r\n");
    pc_printf("  E = motor esquerdo\r\n");
    pc_printf("  B = ambos motores\r\n");
    pc_printf("Qualquer outro caractere -> STOP (S)\r\n\r\n");

    config_radio();

    while (true)
    {

        // -------- 1) Ler comando do terminal e enviar ao servidor --------
        if (pc.readable())
        {
            char c;
            if (pc.read(&c, 1) == 1)
            {
                // eco simples no terminal
                pc.write(&c, 1);

                // Ignorar Enter (CR/LF)
                if (c == '\r' || c == '\n')
                {
                    // não envia comando para o carrinho
                }
                else
                {
                    // Mapeia outros caracteres para STOP
                    char cmd = (c == 'D' || c == 'E' || c == 'B') ? c : 'S';

                    // Prepara payload: primeiro byte = comando, resto preenchido
                    for (int i = 0; i < TRANSFER_SIZE; i++)
                    {
                        tx[i] = 0;
                    }
                    tx[0] = cmd;

                    // Muda para modo TX para enviar comando
                    radio.setTransmitMode();
                    radio.enable();
                    radio.write(NRF24L01P_PIPE_P0, tx, TRANSFER_SIZE);
                    ledTx = !ledTx;

                    // Volta a modo RX para receber telemetria
                    radio.setReceiveMode();
                    radio.enable();
                }
            }
        }

        // -------- 2) Receber distância do servidor e imprimir --------
        if (radio.readable())
        {
            int n = radio.read(NRF24L01P_PIPE_P0, rx, TRANSFER_SIZE);
            if (n > 0)
            {
                // Garante que rx é uma string terminada em '\0'
                char dist_str[TRANSFER_SIZE + 1];
                for (int i = 0; i < TRANSFER_SIZE; i++)
                {
                    dist_str[i] = rx[i];
                }
                dist_str[TRANSFER_SIZE] = '\0';

                pc_printf("\r\n[DIST] %s cm\r\n", dist_str);
            }
        }

        ThisThread::sleep_for(10ms);
    }
}
