#include "mbed.h"
#include <cstdarg>

// --------- Serial PC ----------
BufferedSerial pc(USBTX, USBRX, 115200);

// --------- Motores (L298N) ----------
DigitalOut in1(PTB0); // Motor A (direita)
DigitalOut in2(PTB1);
DigitalOut in3(PTB2); // Motor B (esquerda)
DigitalOut in4(PTB3);

// --------- Encoders ----------
InterruptIn encRight(PTA1); // encoder roda direita
InterruptIn encLeft(PTA2);  // encoder roda esquerda

volatile int32_t pulse_right = 0;
volatile int32_t pulse_left = 0;

// --------- PRINTF helper ----------
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

// --------- Motores ----------
inline void motorA_forward()
{
    in1 = 0;
    in2 = 1;
}

inline void motorB_forward()
{
    in3 = 1;
    in4 = 0;
}

// --------- ISR dos encoders ----------
void on_pulse_right()
{
    pulse_right++;
}

void on_pulse_left()
{
    pulse_left++;
}

int main()
{
    pc_printf("\r\n=== DEBUG: Encoders PTA1/PTA2 + motores para frente ===\r\n");

    // Configuração dos encoders
    encRight.mode(PullUp);
    encLeft.mode(PullUp);

    // Conta borda de subida e descida (se quiser apenas uma, remova a outra)
    encRight.rise(&on_pulse_right);
    encRight.fall(&on_pulse_right);

    encLeft.rise(&on_pulse_left);
    encLeft.fall(&on_pulse_left);

    // Ambos os motores sempre para frente
    motorA_forward();
    motorB_forward();
    pc_printf("Motores ligados para frente.\r\n");

    Timer t;
    t.start();

    const int PRINT_PERIOD_MS = 100; // ajuste conforme necessidade

    while (true)
    {
        if (t.read_ms() >= PRINT_PERIOD_MS)
        {
            t.reset();

            // Cópias locais das variáveis voláteis
            int32_t cR = pulse_right;
            int32_t cL = pulse_left;

            pc_printf("[ENC] PTA1 (Right) = %ld   PTA2 (Left) = %ld\r\n",
                      (long)cR, (long)cL);
        }

        ThisThread::sleep_for(5ms);
    }
}
