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
static inline void stopAll() {
  softPwmWrite(L_FWD, 0); softPwmWrite(L_REV, 0);
  softPwmWrite(R_FWD, 0); softPwmWrite(R_REV, 0);
}

// seta uma roda: dir = +1 (frente), -1 (ré), 0 (parada)
static inline void setLeft(int dir, int pwm) {
  if (dir > 0) { softPwmWrite(L_FWD, pwm); softPwmWrite(L_REV, 0); }
  else if (dir < 0) { softPwmWrite(L_FWD, 0); softPwmWrite(L_REV, pwm); }
  else { softPwmWrite(L_FWD, 0); softPwmWrite(L_REV, 0); }
}
static inline void setRight(int dir, int pwm) {
  if (dir > 0) { softPwmWrite(R_FWD, pwm); softPwmWrite(R_REV, 0); }
  else if (dir < 0) { softPwmWrite(R_FWD, 0); softPwmWrite(R_REV, pwm); }
  else { softPwmWrite(R_FWD, 0); softPwmWrite(R_REV, 0); }
}

// velocidades sugeridas
static constexpr int PWM_HIGH = 90;
static constexpr int PWM_MED  = 60;
static constexpr int PWM_LOW  = 0;

// aplica o comando do teclado ('0','1'..'9'); retorna descrição para overlay
static std::string applyCommand(char cmd) {
  switch (cmd) {
    case '7': // Virar à esquerda (pivot)
      setLeft(-1, PWM_LOW); setRight(+1, PWM_HIGH); return "VIRAR ESQ (7)";
    case '8': // Ir para frente
      setLeft(+1, PWM_HIGH); setRight(+1, PWM_HIGH); return "FRENTE (8)";
    case '9': // Virar à direita (pivot)
      setLeft(+1, PWM_HIGH); setRight(-1, PWM_LOW); return "VIRAR DIR (9)";
    case '4': // Virar acentuadamente à esquerda (leve curva p/ esq)
      setLeft(-1, PWM_HIGH); setRight(+1, PWM_HIGH); return "CURVA ESQ (4)";
    case '6': // Virar acentuadamente à direita
      setLeft(+1, PWM_HIGH); setRight(-1, PWM_HIGH); return "CURVA DIR (6)";
    case '1': // Virar à esquerda dando ré
      setLeft(-1, PWM_LOW); setRight(-1, PWM_HIGH); return "RE ESQ (1)";
    case '2': // Dar ré
      setLeft(-1, PWM_HIGH); setRight(-1, PWM_HIGH); return "RE (2)";
    case '3': // Virar à direita dando ré
      setLeft(-1, PWM_HIGH); setRight(-1, PWM_LOW); return "RE DIR (3)";

    case '5': // Não faz nada
    case '0': // “nada” vindo do cliente
    default:
      stopAll(); return "PARADO (5/0)";
  }
}

// escreve texto do comando no quadro
static void drawCommandOn(cv::Mat& img, const std::string& txt) {
  double scale = std::max(0.6, img.cols / 640.0);
  int th = 2, base = 0;
  cv::Size sz = cv::getTextSize(txt, cv::FONT_HERSHEY_SIMPLEX, scale, th, &base);
  cv::Point org(12, 12 + sz.height);

  // caixa com padding
  int pad = 8;
  cv::Rect box(org.x - pad, org.y - sz.height - pad, sz.width + 2*pad, sz.height + 2*pad);

  // desenha caixa semi-transparente
  cv::Mat overlay = img.clone();
  cv::rectangle(overlay, box, cv::Scalar(0,0,0), cv::FILLED);
  cv::addWeighted(overlay, 0.35, img, 0.65, 0, img);

  // contorno + texto
  cv::putText(img, txt, org, cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0,0,0), 4, cv::LINE_AA);
  cv::putText(img, txt, org, cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0,255,255), 2, cv::LINE_AA);
}

int main() {
  // ---------- wiringPi ----------
  if (wiringPiSetup() == -1) {
    std::cerr << "Erro ao inicializar wiringPi!\n"; return 1;
  }
  softPwmCreate(L_FWD, 0, 100);
  softPwmCreate(L_REV, 0, 100);
  softPwmCreate(R_FWD, 0, 100);
  softPwmCreate(R_REV, 0, 100);
  stopAll();

  // ---------- rede/camera ----------
  SERVER s; s.waitConnection();

  cv::VideoCapture cap(0);
  if (!cap.isOpened()) erro("Nao abriu camera");
  cap.set(cv::CAP_PROP_FRAME_WIDTH,  320);  // 240x320
  cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240);

  Mat_<COR> frame;
  BYTE msg = '0';

  // (2) cliente manda primeiro '0' dizendo que está pronto
  s.receiveBytes(1, &msg);
  if (msg == 's') { stopAll(); return 0; }

  while (true) {
    // (6) Raspberry recebe comando e APLICA nos motores
    char cmd = static_cast<char>(msg);
    std::string label = applyCommand(cmd);

    // captura nova imagem, insere o comando e envia compactada
    cv::Mat raw; cap >> raw;
    if (raw.empty()) { stopAll(); erro("Frame vazio"); }
    raw.copyTo(frame);

    // removido para ex1b
    // drawCommandOn(frame, label);
    s.sendImgComp(frame);

    // aguarda próximo comando: 's' sai; '0' nada; '1'..'9' conforme lista
    s.receiveBytes(1, &msg);
    if (msg == 's') break;
  }

  stopAll();
  return 0;
}
