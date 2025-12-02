#include "projeto.hpp"
#include <opencv2/opencv.hpp>

#include <wiringPi.h>
#include <softPwm.h>
#include <iostream>
#include <cstdint>
#include <chrono>

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
static constexpr int PWM_HIGH = 90;
static constexpr int PWM_LOW = 0;
static constexpr int TURN90_MS = 550;
static constexpr int TURN180_MS = 1000;
static constexpr int PASS_UNDER_MS = 1500;

struct TimedMotion
{
  bool active = false;
  std::chrono::steady_clock::time_point end{};
  int ldir = 0;
  int rdir = 0;
  int lpwm = 0;
  int rpwm = 0;
};

static TimedMotion g_motion;

static void stopTimedMotion()
{
  g_motion.active = false;
  stopAll();
}

static void startTimedMotion(int ldir, int lpwm, int rdir, int rpwm, int durationMs)
{
  stopTimedMotion();
  g_motion.active = true;
  g_motion.ldir = ldir;
  g_motion.lpwm = lpwm;
  g_motion.rdir = rdir;
  g_motion.rpwm = rpwm;
  g_motion.end = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
}

static bool applyTimedMotion()
{
  if (!g_motion.active)
    return false;
  if (std::chrono::steady_clock::now() >= g_motion.end)
  {
    stopTimedMotion();
    return false;
  }
  setLeft(g_motion.ldir, g_motion.lpwm);
  setRight(g_motion.rdir, g_motion.rpwm);
  return true;
}

// --------- comandos antigos (compatibilidade com override) ----------
static bool applyCommand(char cmd)
{
  switch (cmd)
  {
  case '7':
    setLeft(-1, PWM_LOW);
    setRight(+1, PWM_HIGH);
    return true;
  case '8':
    setLeft(+1, PWM_HIGH);
    setRight(+1, PWM_HIGH);
    return true;
  case '9':
    setLeft(+1, PWM_HIGH);
    setRight(-1, PWM_LOW);
    return true;
  case '4':
    setLeft(-1, PWM_HIGH);
    setRight(+1, PWM_HIGH);
    return true;
  case '6':
    setLeft(+1, PWM_HIGH);
    setRight(-1, PWM_HIGH);
    return true;
  case '1':
    setLeft(-1, PWM_LOW);
    setRight(-1, PWM_HIGH);
    return true;
  case '2':
    setLeft(-1, PWM_HIGH);
    setRight(-1, PWM_HIGH);
    return true;
  case '3':
    setLeft(-1, PWM_HIGH);
    setRight(-1, PWM_LOW);
    return true;
  case '5': // fall-through
    return true;
  case '0': // fall-through
    return true;
  }
  return false;
}

static bool handleDigitScript(char cmd)
{
  switch (cmd)
  {
  case '0':
  case '1':
    stopTimedMotion();
    return true;
  case '4':
  case '5':
    startTimedMotion(+1, PWM_HIGH, +1, PWM_HIGH, PASS_UNDER_MS);
    return true;
  case '6':
  case '7':
    startTimedMotion(-1, PWM_HIGH, +1, PWM_HIGH, TURN90_MS);
    return true;
  case '8':
  case '9':
    startTimedMotion(+1, PWM_HIGH, -1, PWM_HIGH, TURN90_MS);
    return true;
  case '2':
    startTimedMotion(-1, PWM_HIGH, +1, PWM_HIGH, TURN180_MS);
    return true;
  case '3':
    startTimedMotion(+1, PWM_HIGH, -1, PWM_HIGH, TURN180_MS);
    return true;
  default:
    return false;
  }
}

// --------- novo: aplica modo analógico contínuo (-100..+100) ----------
static inline int clamp100(int v) { return std::max(-100, std::min(100, v)); }
static void applyAnalog(int8_t l, int8_t r)
{
  int li = clamp100((int)l);
  int ri = clamp100((int)r);

  int ldir = (li > 0) - (li < 0);
  int rdir = (ri > 0) - (ri < 0);

  int lpwm = std::min(PWM_HIGH, std::abs(li));
  int rpwm = std::min(PWM_HIGH, std::abs(ri));

  setLeft(ldir, lpwm);
  setRight(rdir, rpwm);
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

  // estado do último comando recebido (aplicado no início de cada iteração)
  char lastMode = 'K'; // 'K' = comandos 1 byte (teclas), 'A' = analógico
  char lastKey = '0';
  int8_t lastL = 0;
  int8_t lastR = 0;

  // handshake inicial (compatA-vel)
  BYTE first;
  s.receiveBytes(1, &first);
  if (first == 's')
  {
    stopTimedMotion();
    return 0;
  }
  // interpreta primeiro byte como tecla, se nao for 'A'
  if (first == 'A')
  {
    BYTE lr[2];
    s.receiveBytes(2, lr);
    lastMode = 'A';
    lastL = (int8_t)lr[0];
    lastR = (int8_t)lr[1];
  }
  else
  {
    if (!handleDigitScript((char)first))
    {
      lastMode = 'K';
      lastKey = (char)first;
    }
    else
    {
      lastMode = 'K';
      lastKey = '0';
    }
  }
  while (true)
  {
    // (1) aplica o ultimo comando
    if (applyTimedMotion())
    {
      // macro ativa assume o controle
    }
    else if (lastMode == 'A')
      applyAnalog(lastL, lastR);
    else
      applyCommand(lastKey);

    // (2) captura, envia compactado
    cv::Mat raw;
    cap >> raw;
    if (raw.empty())
    {
      stopTimedMotion();
      erro("Frame vazio");
    }
    raw.copyTo(frame);
    s.sendImgComp(frame);

    // (3) recebe próximo comando (cabeçalho de 1 byte)
    BYTE hdr;
    s.receiveBytes(1, &hdr);

    if (hdr == 's')
    {
      stopTimedMotion();
      break;
    }
    else if (hdr == 'A')
    {
      BYTE lr[2];
      s.receiveBytes(2, lr);
      lastMode = 'A';
      lastL = (int8_t)lr[0];
      lastR = (int8_t)lr[1];
    }
    else if (hdr == 'D')
    {
      BYTE digit;
      s.receiveBytes(1, &digit); // segundo byte é o dígito
      if (!handleDigitScript((char)digit))
      {
        lastMode = 'K';
        lastKey = '0'; // ou ignore
      }
    }
    else
    {
      if (!applyCommand((char)hdr))
      {
        lastMode = 'K';
        lastKey = (char)hdr;
      }
      else
      {
        lastMode = 'K';
        lastKey = '0';
      }
    }
  }

  stopTimedMotion();
  return 0;
}
