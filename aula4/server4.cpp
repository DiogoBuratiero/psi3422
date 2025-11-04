#include "projeto.hpp"
#include <opencv2/opencv.hpp>

#include <wiringPi.h>
#include <softPwm.h>
#include <iostream>

// ---------------- PWM (ajuste se seus pinos forem outros) ----------------
static constexpr int R_REV = 0;
static constexpr int R_FWD = 1;
static constexpr int L_FWD = 2;
static constexpr int L_REV = 3;

// Duty-cycle de 0..100
static inline void stopAll()
{
  softPwmWrite(L_FWD, 0);
  softPwmWrite(L_REV, 0);
  softPwmWrite(R_FWD, 0);
  softPwmWrite(R_REV, 0);
}

// seta uma roda: dir = +1 (frente), -1 (ré), 0 (parada)
static inline void setLeft(int dir, int pwm)
{
  if (dir > 0)
  {
    softPwmWrite(L_FWD, pwm);
    softPwmWrite(L_REV, 0);
  }
  else if (dir < 0)
  {
    softPwmWrite(L_FWD, 0);
    softPwmWrite(L_REV, pwm);
  }
  else
  {
    softPwmWrite(L_FWD, 0);
    softPwmWrite(L_REV, 0);
  }
}
static inline void setRight(int dir, int pwm)
{
  if (dir > 0)
  {
    softPwmWrite(R_FWD, pwm);
    softPwmWrite(R_REV, 0);
  }
  else if (dir < 0)
  {
    softPwmWrite(R_FWD, 0);
    softPwmWrite(R_REV, pwm);
  }
  else
  {
    softPwmWrite(R_FWD, 0);
    softPwmWrite(R_REV, 0);
  }
}

// velocidades sugeridas
static constexpr int PWM_HIGH = 90;
static constexpr int PWM_MID_HIGH = 20;
static constexpr int PWM_MID_LOW = 10;
static constexpr int PWM_LOW = 0;

// Aplica o comando vindo do cliente: '0','1'..'9','5','8','4','6','7','9'
static void applyCommand(char cmd)
{
  switch (cmd)
  {
  case '7': // leve-para-esq (assimétrica)
    setLeft(-1, PWM_MID_LOW);
    setRight(+1, PWM_MID_HIGH);
    break;
  case '8': // frente
    setLeft(+1, PWM_HIGH);
    setRight(+1, PWM_HIGH);
    break;
  case '9': // leve-para-dir (assimétrica)
    setLeft(+1, PWM_MID_HIGH);
    setRight(-1, PWM_LOW);
    break;

  case '4': // curva/pivot forte à esquerda
    setLeft(-1, PWM_HIGH);
    setRight(+1, PWM_HIGH);
    break;
  case '6': // curva/pivot forte à direita
    setLeft(+1, PWM_HIGH);
    setRight(-1, PWM_HIGH);
    break;

  case '1': // leve-para-esq ré
    setLeft(-1, PWM_LOW);
    setRight(-1, PWM_HIGH);
    break;
  case '2': // ré
    setLeft(-1, PWM_HIGH);
    setRight(-1, PWM_HIGH);
    break;
  case '3': // leve-para-dir ré
    setLeft(-1, PWM_HIGH);
    setRight(-1, PWM_LOW);
    break;

  case '5': // parado

  case '0': // nada

  default:
    stopAll();
    break;
  }
}

int main()
{
  // ---------- wiringPi ----------
  if (wiringPiSetup() == -1)
  {
    std::cerr << "Erro ao inicializar wiringPi!\n";
    return 1;
  }
  softPwmCreate(L_FWD, 0, 100);
  softPwmCreate(L_REV, 0, 100);
  softPwmCreate(R_FWD, 0, 100);
  softPwmCreate(R_REV, 0, 100);
  stopAll();

  // ---------- rede/camera ----------
  SERVER s;
  s.waitConnection();

  cv::VideoCapture cap(0);
  if (!cap.isOpened())
    erro("Nao abriu camera");
  cap.set(cv::CAP_PROP_FRAME_WIDTH, 320); // 240x320
  cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240);

  Mat_<COR> frame;
  BYTE msg = '0';

  // (2) cliente manda primeiro '0' dizendo que está pronto
  s.receiveBytes(1, &msg);
  if (msg == 's')
  {
    stopAll();
    return 0;
  }

  while (true)
  {
    // (6) aplica comando
    applyCommand(static_cast<char>(msg));

    // captura, envia compactado, aguarda próximo byte
    cv::Mat raw;
    cap >> raw;
    if (raw.empty())
    {
      stopAll();
      erro("Frame vazio");
    }
    raw.copyTo(frame);
    s.sendImgComp(frame);

    s.receiveBytes(1, &msg);
    if (msg == 's')
      break;
  }

  stopAll();
  return 0;
}
