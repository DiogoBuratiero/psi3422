#include "projeto.hpp"
#include <opencv2/opencv.hpp>
#include <chrono>

using std::vector;

static int g_pressed = 0; // 0 nada; 1..9 botão (ativo enquanto segurar)
static bool g_mouseDown = false;
static int g_cols = 320, g_rows = 240;

// Mapa no layout numpad (teclado à ESQUERDA, câmera à DIREITA)
static const int kmap[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};

static inline int key_at_xy_on_keyboard(int x, int y)
{
  // x,y relativos ao teclado (teclado ocupa [0..g_cols))
  const int cellW = g_cols / 3, cellH = g_rows / 3;
  if (x < 0 || x >= g_cols || y < 0 || y >= g_rows)
    return 0;
  int c = x / cellW, l = y / cellH;
  if (0 <= l && l < 3 && 0 <= c && c < 3)
    return kmap[l][c];
  return 0;
}

static void on_mouse(int event, int x, int y, int flags, void *)
{
  // Teclado está à ESQUERDA: [0 .. g_cols). Câmera à direita: [g_cols .. 2*g_cols)
  const bool sobreTeclado = (x >= 0 && x < g_cols && y >= 0 && y < g_rows);

  if (event == cv::EVENT_LBUTTONDOWN)
  {
    g_mouseDown = true;
    if (sobreTeclado)
    {
      g_pressed = key_at_xy_on_keyboard(x, y);
    }
    else
    {
      g_pressed = 0;
    }
  }
  else if (event == cv::EVENT_MOUSEMOVE)
  {
    if (g_mouseDown)
    {
      if (sobreTeclado)
      {
        g_pressed = key_at_xy_on_keyboard(x, y);
      }
      else
      {
        // saiu do teclado enquanto mantém pressionado
        g_pressed = 0;
      }
    }
  }
  else if (event == cv::EVENT_LBUTTONUP)
  {
    g_mouseDown = false;
    g_pressed = 0; // soltar o botão do mouse limpa o comando
  }
}

// Desenha o teclado; se 'activeKey' == 1..9, realça essa seta em vermelho vivo
static cv::Mat makeKeyboard(int w, int h, int activeKey)
{
  using namespace cv;

  Mat kb(h, w, CV_8UC3, Scalar(55, 55, 55));
  const int cellW = w / 3, cellH = h / 3;
  const int gridT = std::max(1, std::min(cellW, cellH) / 90);
  const int thick = std::max(2, std::min(cellW, cellH) / 18);
  const int thickActive = thick + std::max(1, thick / 2);
  const double tip = 0.25; // ponta da seta
  const Scalar gridCol(90, 90, 90);
  const Scalar arrowColBase(0, 0, 150);       // cor padrão (mais escura)
  const Scalar arrowColActive(0, 0, 255);     // vermelho vivo (ativa)
  const Scalar arrowColStrongBase(0, 0, 200); // destaque leve padrão para '8'

  // grade
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      rectangle(kb, Rect(c * cellW, r * cellH, cellW, cellH), gridCol, gridT);

  auto cellRC = [&](int r, int c)
  { return Rect(c * cellW, r * cellH, cellW, cellH); };
  auto center = [&](const Rect &rc)
  { return Point(rc.x + rc.width / 2, rc.y + rc.height / 2); };
  const int m = std::min(cellW, cellH) / 5;

  auto arrow = [&](Point p1, Point p2, bool strong, bool active)
  {
    const Scalar col = active ? arrowColActive : (strong ? arrowColStrongBase : arrowColBase);
    const int tk = active ? thickActive : thick;
    arrowedLine(kb, p1, p2, col, tk, LINE_AA, 0, tip);
  };
  auto lineSeg = [&](Point p1, Point p2, bool active)
  {
    const Scalar col = active ? arrowColActive : arrowColBase;
    const int tk = active ? thickActive : thick;
    line(kb, p1, p2, col, tk, LINE_AA);
  };

  auto isActive = [&](int val)
  { return activeKey == val; };

  // 8 (frente) — seta para cima no centro superior
  {
    Rect rc = cellRC(0, 1);
    Point c0 = center(rc);
    arrow(Point(c0.x, c0.y + m), Point(c0.x, c0.y - m),
          /*strong=*/true, /*active=*/isActive(8));
  }

  // 2 (ré) — seta para baixo no centro inferior
  {
    Rect rc = cellRC(2, 1);
    Point c0 = center(rc);
    arrow(Point(c0.x, c0.y - m), Point(c0.x, c0.y + m),
          /*strong=*/false, /*active=*/isActive(2));
  }

  // 7, 9 (diagonais para cima)
  {
    Rect rc7 = cellRC(0, 0);
    Point c7 = center(rc7);
    arrow(Point(c7.x + m * 0.7, c7.y + m * 0.7), Point(c7.x - m * 0.7, c7.y - m * 0.7),
          /*strong=*/false, /*active=*/isActive(7));

    Rect rc9 = cellRC(0, 2);
    Point c9 = center(rc9);
    arrow(Point(c9.x - m * 0.7, c9.y + m * 0.7), Point(c9.x + m * 0.7, c9.y - m * 0.7),
          /*strong=*/false, /*active=*/isActive(9));
  }

  // 1, 3 (diagonais para baixo)
  {
    Rect rc1 = cellRC(2, 0);
    Point c1 = center(rc1);
    arrow(Point(c1.x + m * 0.7, c1.y - m * 0.7), Point(c1.x - m * 0.7, c1.y + m * 0.7),
          /*strong=*/false, /*active=*/isActive(1));

    Rect rc3 = cellRC(2, 2);
    Point c3 = center(rc3);
    arrow(Point(c3.x - m * 0.7, c3.y - m * 0.7), Point(c3.x + m * 0.7, c3.y + m * 0.7),
          /*strong=*/false, /*active=*/isActive(3));
  }

  // 4 (virar acentuadamente à esquerda) — “L”
  {
    Rect rc = cellRC(1, 0);
    Point c0 = center(rc);
    lineSeg(Point(c0.x, c0.y + m), Point(c0.x, c0.y - m / 3), isActive(4));
    arrow(Point(c0.x, c0.y - m / 3), Point(c0.x - m, c0.y - m / 3),
          /*strong=*/false, /*active=*/isActive(4));
  }

  // 6 (virar acentuadamente à direita) — “┘” espelhado
  {
    Rect rc = cellRC(1, 2);
    Point c0 = center(rc);
    lineSeg(Point(c0.x, c0.y + m), Point(c0.x, c0.y - m / 3), isActive(6));
    arrow(Point(c0.x, c0.y - m / 3), Point(c0.x + m, c0.y - m / 3),
          /*strong=*/false, /*active=*/isActive(6));
  }

  // 5 (nada) — ponto central
  {
    Rect rc = cellRC(1, 1);
    Point c0 = center(rc);
    int r = std::max(3, std::min(cellW, cellH) / 18);
    const cv::Scalar col = isActive(5) ? arrowColActive : arrowColBase;
    circle(kb, c0, r, col, FILLED, LINE_AA);
  }

  putText(kb, "ESC = sair", {10, h - 8},
          FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 200, 255), 1, LINE_AA);

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
  char mode = (argc == 4 ? argv[3][0] : 't'); // 't' = grava tela; 'c' = só camera

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
  double fpsHint = 18.0;

  BYTE out = '0';
  int frames = 0;
  double t1 = nowSec();

  while (true)
  {
    Mat_<COR> cam;
    c.receiveImgComp(cam); // 240x320 JPEG do servidor

    g_cols = cam.cols;
    g_rows = cam.rows;

    // passa a tecla ativa para desenhar em vermelho
    cv::Mat kb = makeKeyboard(cam.cols, cam.rows, g_pressed);

    // tela = teclado | camera (câmera à direita)
    cv::Mat tela;
    cv::hconcat(kb, cv::Mat(cam), tela);

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
        wr << tela; // grava tela (default 't')
    }

    // Envio contínuo: mantém comando enquanto mouse estiver pressionando uma célula
    int ch = cv::waitKey(1) & 0xFF;
    if (ch == 27)
      out = 's'; // ESC
    else
      out = (g_pressed >= 1 && g_pressed <= 9) ? char('0' + g_pressed) : '0';

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
