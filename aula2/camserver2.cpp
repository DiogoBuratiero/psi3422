#include "new_projeto.hpp"

int main()
{
  SERVER s;
  s.waitConnection();

  cv::VideoCapture cap(0);
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
      erro("Frame vazio");
    raw.copyTo(frame);

    s.sendImgComp(frame);    // envia COM compressão
    s.receiveBytes(1, &ack); // handshake: espera cliente ('0' continua, 's' sai)  :contentReference[oaicite:9]{index=9}
    if (ack == 's')
      break;
  }
  return 0;
}
