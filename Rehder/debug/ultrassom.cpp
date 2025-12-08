#include "mbed.h"
#include <cstdarg>

// ---------- Serial PC ----------
BufferedSerial pc(USBTX, USBRX, 115200);

// ---------- Motores (L298N) ----------
DigitalOut in1(PTB0); // Motor A (direita)
DigitalOut in2(PTB1);
DigitalOut in3(PTB2); // Motor B (esquerda)
DigitalOut in4(PTB3);

// ---------- HC-SR04: trigger e echo ----------
DigitalOut trig(PTC7);  // Trigger
InterruptIn echo(PTA4); // Echo (saída do sensor)

// ---------- Variáveis de medida ----------
Timer echoTimer; // base de tempo em µs
volatile uint32_t echo_start_us = 0;
volatile uint32_t echo_pulse_us = 0;
volatile bool new_measure = false;

// ---------- PRINTF helper (sem %f) ----------
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

// Converte float em string com 1 casa decimal (ex: "12.3")
void float_to_str1(char *out, float v)
{
    // Escala para décimos
    int32_t scaled = (int32_t)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f));
    if (scaled < 0)
        scaled = -scaled; // se quiser lidar com negativo

    int32_t inteiro = scaled / 10;
    int32_t frac = scaled % 10;

    // Formato: X.Y (ex: 12.3)
    snprintf(out, 16, "%ld.%01ld", (long)inteiro, (long)frac);
}

// ---------- Motores sempre para frente ----------
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

// ---------- HC-SR04: funções de medição ----------

// Envia pulso de trigger (~10 µs)
void send_trigger_pulse()
{
    trig = 0;
    wait_us(2);
    trig = 1;
    wait_us(10);
    trig = 0;
}

// Echo rising: início do pulso
void echo_rise()
{
    echo_start_us = echoTimer.read_us();
}

// Echo falling: fim do pulso
void echo_fall()
{
    uint32_t end_us = echoTimer.read_us();
    echo_pulse_us = end_us - echo_start_us;
    new_measure = true;
}

int main()
{
    pc_printf("\r\n=== DEBUG: HC-SR04 em PTC7 (trig) / PTA4 (echo, InterruptIn) ===\r\n");

    // Motores pra frente
    motorA_forward();
    motorB_forward();
    pc_printf("Motores ligados para frente.\r\n");

    // Configuração do HC-SR04
    trig = 0; // nível baixo inicialmente
    echo.rise(&echo_rise);
    echo.fall(&echo_fall);

    echoTimer.start();

    Timer loopTimer;
    loopTimer.start();

    const int TRIGGER_PERIOD_MS = 100; // mede aprox. a cada 100 ms

    while (true)
    {

        // Dispara um novo pulso de trigger periodicamente
        if (loopTimer.read_ms() >= TRIGGER_PERIOD_MS)
        {
            loopTimer.reset();
            send_trigger_pulse();
        }

        // Se chegou uma nova medida do echo, calcula distância
        if (new_measure)
        {
            new_measure = false;

            uint32_t dur = echo_pulse_us; // duração em µs

            // Distância em cm: dist = dur * 0.0343 / 2 = dur * 0.01715
            float dist_cm = dur * 0.01715f;

            char distStr[16];
            float_to_str1(distStr, dist_cm);

            pc_printf("[DIST] dur_us=%lu   dist_cm=%s\r\n",
                      (unsigned long)dur, distStr);
        }

        ThisThread::sleep_for(5ms);
    }
}
