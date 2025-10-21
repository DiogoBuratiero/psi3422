// pwmroda4_simple.cpp
// Compile: g++ pwmroda4_simple.cpp -o pwmroda4_simple -lwiringPi -lpthread
// Run (via SSH or VNC): sudo ./pwmroda4_simple

#include <wiringPi.h>
#include <softPwm.h>
#include <iostream>

int main()
{
    if (wiringPiSetup() == -1)
    {
        std::cerr << "Erro ao inicializar wiringPi!" << std::endl;
        return 1;
    }

    // Cria saídas PWM para os 4 pinos do driver de motor
    softPwmCreate(0, 0, 100);
    softPwmCreate(1, 0, 100);
    softPwmCreate(2, 0, 100);
    softPwmCreate(3, 0, 100);

    std::cout << "Iniciando giros para a direita..." << std::endl;

    // Giro para direita: esquerda para frente (pino 0), direita para trás (pino 2)
    for (int i = 0; i < 2; i++)
    {
        std::cout << "Giro " << (i + 1) << " em alta velocidade (90%)" << std::endl;
        softPwmWrite(0, 90); // esquerda frente
        softPwmWrite(1, 0);
        softPwmWrite(2, 90); // direita trás
        softPwmWrite(3, 0);
        delay(2000); // tempo de giro

        // Para o carrinho
        softPwmWrite(0, 0);
        softPwmWrite(2, 0);
        delay(1000);
    }

    for (int i = 0; i < 2; i++)
    {
        std::cout << "Giro " << (i + 1) << " em meia velocidade (60%)" << std::endl;
        softPwmWrite(0, 60); // esquerda frente
        softPwmWrite(1, 0);
        softPwmWrite(2, 60); // direita trás
        softPwmWrite(3, 0);
        delay(2000);

        // Para o carrinho
        softPwmWrite(0, 0);
        softPwmWrite(2, 0);
        delay(1000);
    }

    std::cout << "Parando PWM e finalizando." << std::endl;
    softPwmStop(0);
    softPwmStop(1);
    softPwmStop(2);
    softPwmStop(3);

    return 0;
}
