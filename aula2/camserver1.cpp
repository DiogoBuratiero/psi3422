#include "new_projeto.hpp"

int main()
{
  SERVER s;
  s.waitConnection();

  cv::VideoCapture cap(0); // câmera padrão
  if (!cap.isOpened())
    erro("Nao abriu camera");
  cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
  cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

  Mat_<COR> frame;
  BYTE ack = '0';

  while (true)
  {
    cv::Mat raw;
    cap >> raw;
    if (raw.empty())
      erro("Frame vazio da camera");
    raw.copyTo(frame); // garante Mat_<COR> (Vec3b)

    s.sendImg(frame); // envia header + payload
    // espera confirmação do cliente antes de enviar o próximo
    s.receiveBytes(1, &ack);
    if (ack == 's')
      break; // cliente pediu para sair
  }

  return 0;
}
