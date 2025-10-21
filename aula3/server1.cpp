#include "projeto.hpp"
#include <opencv2/opencv.hpp>

static void drawCommandOn(cv::Mat &img, char cmd)
{
  cv::putText(img, std::string("CMD: ") + cmd, {10, 24},
              cv::FONT_HERSHEY_SIMPLEX, 0.8, {0, 255, 255}, 2, cv::LINE_AA);
}

int main()
{
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
    return 0;

  while (true)
  {
    cv::Mat raw;
    cap >> raw;
    if (raw.empty())
      erro("Frame vazio");
    raw.copyTo(frame);

    // (6) insere a mensagem recebida no quadro ANTES de enviar o próximo
    drawCommandOn(frame, (char)msg);

    // envia imagem COMPACTADA (3)
    s.sendImgComp(frame);

    // espera resposta: 's' termina; '0' nada; '1'..'9' botão virtual (6)
    s.receiveBytes(1, &msg);
    if (msg == 's')
      break;
  }
  return 0;
}
