#include "new_projeto.hpp"

int main(int argc, char *argv[])
{
  if (argc != 2)
    erro("camclient1 servidorIp\n");
  CLIENT c(argv[1]);

  cv::namedWindow("camclient1", cv::WINDOW_AUTOSIZE);

  double t1 = timeSinceEpoch();
  int frames = 0;
  BYTE ack = '0';

  while (true)
  {
    Mat_<COR> img;
    c.receiveImg(img); // recebe 480x640 BGR
    cv::imshow("camclient1", img);

    int ch = cv::waitKey(1); // 1ms
    frames++;

    // envia confirmação para o servidor
    if (ch == 27 /*ESC*/)
    {
      ack = 's';
    }
    else
    {
      ack = '0';
    }
    c.sendBytes(1, &ack);
    if (ack == 's')
      break;
  }

  double t2 = timeSinceEpoch();
  double dt = t2 - t1;
  double fps = (dt <= 0.0) ? 0.0 : frames / dt;
  std::printf("Quadros=%d tempo=%0.2fs fps=%0.2f\n", frames, dt, fps);

  return 0;
}
