#include "projeto.hpp"
#include <opencv2/opencv.hpp>
#include <chrono>

using std::vector;

static int g_pressed = 0; // 0 nada; 1..9 botão
static int g_cols = 320, g_rows = 240;

static void on_mouse(int event, int x, int y, int, void *)
{
  if (event != cv::EVENT_LBUTTONDOWN)
    return;
  if (x < g_cols)
  {
    g_pressed = 0;
    return;
  } // clique na câmera = ignora
  int xr = x - g_cols;
  int cellW = g_cols / 3, cellH = g_rows / 3;
  int c = xr / cellW, l = y / cellH;
  static int map[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  if (0 <= l && l < 3 && 0 <= c && c < 3)
    g_pressed = map[l][c];
}

static cv::Mat makeKeyboard(int w, int h)
{
  cv::Mat kb(h, w, CV_8UC3, cv::Scalar(40, 40, 40));
  int cellW = w / 3, cellH = h / 3;
  const char *labels[9] = {
      "7 virar esq.", "8 frente", "9 virar dir.",
      "4 vira levemente E", "5 nada", "6 vira levemente D",
      "1 vira E dando re", "2 re", "3 vira D dando re"};
  int k = 0;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c, ++k)
    {
      cv::Rect rc(c * cellW, r * cellH, cellW - 1, cellH - 1);
      cv::rectangle(kb, rc, {200, 200, 200}, 1);
      cv::putText(kb, std::to_string(k + 1), {rc.x + 6, rc.y + 20},
                  cv::FONT_HERSHEY_SIMPLEX, 0.6, {0, 255, 0}, 1, cv::LINE_AA);
      cv::putText(kb, labels[k],
                  {rc.x + 6, rc.y + cellH / 2 + 10},
                  cv::FONT_HERSHEY_SIMPLEX, 0.45, {0, 220, 255}, 1, cv::LINE_AA);
    }
  cv::putText(kb, "ESC = sair", {10, h - 8},
              cv::FONT_HERSHEY_SIMPLEX, 0.5, {0, 200, 255}, 1, cv::LINE_AA);
  return kb;
}

static inline double nowSec()
{
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char *argv[])
{
  if (argc < 2 || argc > 4)
  {
    std::cerr << "uso: cliente1 servidorIp [videosaida.avi] [t/c]\n";
    return 1;
  }
  const char *ip = argv[1];
  const char *outName = (argc >= 3 ? argv[2] : nullptr);
  char mode = (argc == 4 ? argv[3][0] : 't'); // 't' = grava tela (default); 'c' = só camera

  CLIENT c(ip);
  cv::namedWindow("cliente1", cv::WINDOW_AUTOSIZE);
  cv::setMouseCallback("cliente1", on_mouse);

  // (2) avisa que está pronto
  {
    BYTE start = '0';
    c.sendBytes(1, &start);
  }

  cv::VideoWriter wr;
  bool wrOpen = false;
  int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
  double fpsHint = 18.0; // referência com JPEG na apostila

  BYTE out = '0';
  int frames = 0;
  double t1 = nowSec();

  while (true)
  {
    Mat_<COR> cam;
    c.receiveImgComp(cam); // 240x320 JPEG do servidor

    g_cols = cam.cols;
    g_rows = cam.rows;
    cv::Mat kb = makeKeyboard(cam.cols, cam.rows);

    // tela = camera | teclado
    cv::Mat tela;
    cv::hconcat(cv::Mat(cam), kb, tela);

    cv::imshow("cliente1", tela);

    // abrir writer no primeiro frame se pediu vídeo
    if (outName && !wrOpen)
    {
      cv::Size sz = (mode == 'c' ? cv::Size(cam.cols, cam.rows) : tela.size());
      wr.open(outName, fourcc, fpsHint, sz, true);
      if (!wr.isOpened())
        erro("Falha ao abrir VideoWriter");
      wrOpen = true;
    }
    if (wrOpen)
    {
      if (mode == 'c')
        wr << cv::Mat(cam); // grava só câmera
      else
        wr << tela; // grava tela (default t)
    }

    int ch = cv::waitKey(1) & 0xFF;
    if (ch == 27)
      out = 's'; // ESC
    else if (g_pressed >= 1 && g_pressed <= 9)
    {
      out = char('0' + g_pressed);
      g_pressed = 0; // 1..9
    }
    else
      out = '0'; // nada

    c.sendBytes(1, &out); // (5) envia 's'/'0'/'1'..'9'
    if (out == 's')
      break;

    frames++;
  }

  if (wrOpen)
    wr.release();

  double dt = nowSec() - t1;
  if (dt > 0)
    std::printf("Quadros=%d tempo=%.2fs fps=%.2f\n", frames, dt, frames / dt);
  return 0;
}
