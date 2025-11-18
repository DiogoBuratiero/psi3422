// servidor6.cpp — Executa comandos enviados pelo cliente6 (dígitos em placas)
// Uso: ./servidor6

#include "projeto.hpp"
#include <opencv2/opencv.hpp>
#include <wiringPi.h>
#include <softPwm.h>
#include <chrono>
#include <cstdint>
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

// ganhos/escala
static constexpr int PWM_MAX = 100;
static constexpr float PWM_BALANCE = 1.7f; // ajuste fino para compensar lado direito

static inline double nowSec()
{
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

enum class Action
{
  Stop,
  Forward,
  SpinLeft,
  SpinRight
};

struct Motion
{
  Action act = Action::Stop;
  double until = 0.0; // timestamp absoluto (segundos)
};

static void applyMotion(const Motion &m, int pwmFwd, int pwmTurn)
{
  switch (m.act)
  {
  case Action::Forward:
    setLeft(+1, pwmFwd);
    setRight(+1, int(std::round(PWM_BALANCE * pwmFwd)));
    break;
  case Action::SpinLeft:
    setLeft(-1, pwmTurn);
    setRight(+1, int(std::round(PWM_BALANCE * pwmTurn)));
    break;
  case Action::SpinRight:
    setLeft(+1, pwmTurn);
    setRight(-1, int(std::round(PWM_BALANCE * pwmTurn)));
    break;
  case Action::Stop:
  default:
    stopAll();
    break;
  }
}

static Motion motionFromCommand(char cmd, double now,
                                double durForward, double dur90, double dur180)
{
  Motion m;
  switch (cmd)
  {
  case '4':
  case '5': // seguir em frente sob a placa
    m.act = Action::Forward;
    m.until = now + durForward;
    break;
  case '6':
  case '7': // 90 esq
    m.act = Action::SpinLeft;
    m.until = now + dur90;
    break;
  case '8':
  case '9': // 90 dir
    m.act = Action::SpinRight;
    m.until = now + dur90;
    break;
  case '2': // 180 esq
    m.act = Action::SpinLeft;
    m.until = now + dur180;
    break;
  case '3': // 180 dir
    m.act = Action::SpinRight;
    m.until = now + dur180;
    break;
  case '0':
  case '1':
  default:
    m.act = Action::Stop;
    m.until = now;
    break;
  }
  return m;
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
  cap.set(cv::CAP_PROP_FRAME_WIDTH, 320);
  cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240);

  Mat_<COR> frame;

  // parâmetros de movimento (ajuste fino conforme seu robô)
  const int PWM_FWD = 80;
  const int PWM_TURN = 90;
  const double DUR_FORWARD = 1.0; // segundos
  const double DUR_TURN_90 = 0.65;
  const double DUR_TURN_180 = 1.25;

  Motion motion{Action::Stop, 0.0};
  char lastCmd = '0';

  // handshake inicial compatível
  BYTE first;
  s.receiveBytes(1, &first);
  if (first == 's')
  {
    stopAll();
    return 0;
  }
  lastCmd = (char)first;
  motion = motionFromCommand(lastCmd, nowSec(), DUR_FORWARD, DUR_TURN_90, DUR_TURN_180);

  while (true)
  {
    double tnow = nowSec();
    if (tnow >= motion.until)
    {
      motion.act = Action::Stop;
      motion.until = tnow;
    }

    applyMotion(motion, PWM_FWD, PWM_TURN);

    // captura e envia
    cv::Mat raw;
    cap >> raw;
    if (raw.empty())
    {
      stopAll();
      erro("Frame vazio");
    }
    raw.copyTo(frame);
    s.sendImgComp(frame);

    // recebe próximo comando
    BYTE hdr;
    s.receiveBytes(1, &hdr);
    if (hdr == 's')
    {
      break;
    }

    lastCmd = (char)hdr;
    motion = motionFromCommand(lastCmd, nowSec(), DUR_FORWARD, DUR_TURN_90, DUR_TURN_180);

    std::cerr << "Cmd=" << lastCmd << " ate " << motion.until << "\n";
  }

  stopAll();
  return 0;
}
