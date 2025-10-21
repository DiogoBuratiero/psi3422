#include "new_projeto.hpp"
#include <chrono>

static inline double nowSec()
{
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char *argv[])
{
  if (argc != 2)
    erro("camclient2 servidorIp\n");
  CLIENT c(argv[1]);

  cv::namedWindow("camclient2", cv::WINDOW_AUTOSIZE);

  double t1 = nowSec();
  int frames = 0;
  BYTE ack = '0';

  while (true)
  {
    Mat_<COR> img;
    c.receiveImgComp(img); // recebe + descompacta
    cv::imshow("camclient2", img);
    frames++;

    int ch = cv::waitKey(1);
    ack = (ch == 27 /*ESC*/) ? 's' : '0';
    c.sendBytes(1, &ack); // confirma e controla fluxo (ACK)  :contentReference[oaicite:10]{index=10}
    if (ack == 's')
      break;
  }

  double t2 = nowSec();
  double dt = t2 - t1;
  double fps = (dt > 0) ? frames / dt : 0.0;
  std::printf("Quadros=%d tempo=%.2fs fps=%.2f\n", frames, dt, fps);

  return 0;
}
