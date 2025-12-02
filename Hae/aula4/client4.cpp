// cliente4.cpp — Cliente com controle por "teclado de mouse" (override) + seguir quadrado.png
// Uso:   ./cliente4 <ip_raspberry> [videosaida.avi] [t/c]
// Comp.: g++ -std=c++17 cliente4.cpp -o cliente4 `pkg-config --cflags --libs opencv4` -fopenmp

#include "projeto.hpp"
#include <opencv2/opencv.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#ifdef _OPENMP
#include <omp.h>
#endif

using std::vector;

// ---------- Estado do "teclado" por cursor ----------
static int g_pressed = 0; // 0 nada; 1..9 enquanto cursor pressionado na célula
static bool g_mouseDown = false;
static int g_cols = 320, g_rows = 240;

// Mapa no layout numpad (teclado à ESQUERDA, câmera à DIREITA)
static const int kmap[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};

static inline int key_at_xy_on_keyboard(int x, int y)
{
  const int cellW = g_cols / 3, cellH = g_rows / 3;
  if (x < 0 || x >= g_cols || y < 0 || y >= g_rows)
    return 0;
  int c = x / cellW, l = y / cellH;
  if (0 <= l && l < 3 && 0 <= c && c < 3)
    return kmap[l][c];
  return 0;
}

static void on_mouse(int event, int x, int y, int /*flags*/, void *)
{
  const bool sobreTeclado = (x >= 0 && x < g_cols && y >= 0 && y < g_rows);
  if (event == cv::EVENT_LBUTTONDOWN)
  {
    g_mouseDown = true;
    g_pressed = sobreTeclado ? key_at_xy_on_keyboard(x, y) : 0;
  }
  else if (event == cv::EVENT_MOUSEMOVE)
  {
    if (g_mouseDown)
      g_pressed = sobreTeclado ? key_at_xy_on_keyboard(x, y) : 0;
  }
  else if (event == cv::EVENT_LBUTTONUP)
  {
    g_mouseDown = false;
    g_pressed = 0;
  }
}

// ---------- Desenho do teclado visual ----------
static cv::Mat makeKeyboard(int w, int h, int activeKey)
{
  using namespace cv;
  Mat kb(h, w, CV_8UC3, Scalar(55, 55, 55));
  const int cellW = w / 3, cellH = h / 3;
  const int gridT = std::max(1, std::min(cellW, cellH) / 90);
  const int thick = std::max(2, std::min(cellW, cellH) / 18);
  const int thickActive = thick + std::max(1, thick / 2);
  const double tip = 0.25;
  const Scalar gridCol(90, 90, 90), arrowColBase(0, 0, 150), arrowColActive(0, 0, 255), arrowColStrongBase(0, 0, 200);

  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      rectangle(kb, Rect(c * cellW, r * cellH, cellW, cellH), gridCol, gridT);

  auto cellRC = [&](int r, int c)
  { return Rect(c * cellW, r * cellH, cellW, cellH); };
  auto center = [&](const Rect &rc)
  { return cv::Point(rc.x + rc.width / 2, rc.y + rc.height / 2); };
  auto isActive = [&](int v)
  { return activeKey == v; };
  const int m = std::min(cellW, cellH) / 5;

  auto arrow = [&](cv::Point p1, cv::Point p2, bool strong, bool active)
  {
    const Scalar col = active ? arrowColActive : (strong ? arrowColStrongBase : arrowColBase);
    const int tk = active ? thickActive : thick;
    arrowedLine(kb, p1, p2, col, tk, LINE_AA, 0, tip);
  };
  auto lineSeg = [&](cv::Point p1, cv::Point p2, bool active)
  {
    const Scalar col = active ? arrowColActive : arrowColBase;
    const int tk = active ? thickActive : thick;
    line(kb, p1, p2, col, tk, LINE_AA);
  };

  // 8
  {
    auto rc = cellRC(0, 1);
    auto c0 = center(rc);
    arrow({c0.x, c0.y + m}, {c0.x, c0.y - m}, false, isActive(8));
  }
  // 2
  {
    auto rc = cellRC(2, 1);
    auto c0 = center(rc);
    arrow({c0.x, c0.y - m}, {c0.x, c0.y + m}, false, isActive(2));
  }
  // 7,9
  {
    auto rc = cellRC(0, 0);
    auto c0 = center(rc);
    arrow({int(c0.x + m * 0.7), int(c0.y + m * 0.7)}, {int(c0.x - m * 0.7), int(c0.y - m * 0.7)}, false, isActive(7));
    rc = cellRC(0, 2);
    c0 = center(rc);
    arrow({int(c0.x - m * 0.7), int(c0.y + m * 0.7)}, {int(c0.x + m * 0.7), int(c0.y - m * 0.7)}, false, isActive(9));
  }
  // 1,3
  {
    auto rc = cellRC(2, 0);
    auto c0 = center(rc);
    arrow({int(c0.x + m * 0.7), int(c0.y - m * 0.7)}, {int(c0.x - m * 0.7), int(c0.y + m * 0.7)}, false, isActive(1));
    rc = cellRC(2, 2);
    c0 = center(rc);
    arrow({int(c0.x - m * 0.7), int(c0.y - m * 0.7)}, {int(c0.x + m * 0.7), int(c0.y + m * 0.7)}, false, isActive(3));
  }
  // 4
  {
    auto rc = cellRC(1, 0);
    auto c0 = center(rc);
    lineSeg({c0.x, c0.y + m}, {c0.x, c0.y - m / 3}, isActive(4));
    arrow({c0.x, c0.y - m / 3}, {c0.x - m, c0.y - m / 3}, false, isActive(4));
  }
  // 6
  {
    auto rc = cellRC(1, 2);
    auto c0 = center(rc);
    lineSeg({c0.x, c0.y + m}, {c0.x, c0.y - m / 3}, isActive(6));
    arrow({c0.x, c0.y - m / 3}, {c0.x + m, c0.y - m / 3}, false, isActive(6));
  }
  // 5
  {
    auto rc = cellRC(1, 1);
    auto c0 = center(rc);
    int r = std::max(3, std::min(cellW, cellH) / 18);
    circle(kb, c0, r, isActive(5) ? arrowColActive : arrowColBase, cv::FILLED, cv::LINE_AA);
  }

  cv::putText(kb, "ESC = sair", {10, h - 8}, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 200, 255), 1, cv::LINE_AA);
  return kb;
}

// ---------- util: tempo ----------
static inline double nowSec()
{
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---------- Localização do quadrado (CC+NCC paralelos) ----------
using namespace cv;
struct Cand
{
  int l = 0, c = 0, k = 0;
  float cc = -1.f, ncc = -1.f;
};

static vector<double> geoScales(double s_min, double s_max, int N)
{
  vector<double> s(N);
  double g = std::pow(s_max / s_min, 1.0 / (N - 1)), v = s_min;
  for (int i = 0; i < N; ++i)
  {
    s[i] = v;
    v *= g;
  }
  return s;
}
static void suppressNeighborhood(Mat_<float> &R, int l, int c, int r, float val = 0.f)
{
  int L = std::max(0, l - r), Rl = std::min(R.rows - 1, l + r);
  int C = std::max(0, c - r), Cr = std::min(R.cols - 1, c + r);
  for (int y = L; y <= Rl; ++y)
    for (int x = C; x <= Cr; ++x)
    {
      int dy = y - l, dx = x - c;
      if (dy * dy + dx * dx <= r * r)
        R(y, x) = val;
    }
}
static vector<Cand> topKWithSeparation(const vector<Mat_<float>> &ccMaps, const vector<Size> &tsz, int K, int minDist)
{
  vector<Cand> out;
  vector<Mat_<float>> maps;
  maps.reserve(ccMaps.size());
  for (auto &m : ccMaps)
    maps.push_back(m.clone());
  for (int t = 0; t < K; ++t)
  {
    float best = -1.f;
    int bk = -1, bl = -1, bc = -1;
    for (int k = 0; k < (int)maps.size(); ++k)
    {
      double minv, maxv;
      Point minp, maxp;
      minMaxLoc(maps[k], &minv, &maxv, &minp, &maxp);
      if (maxv > best)
      {
        best = (float)maxv;
        bk = k;
        bl = maxp.y;
        bc = maxp.x;
      }
    }
    if (bk < 0)
      break;
    out.push_back({bl, bc, bk, best, -1.f});
    for (int k = 0; k < (int)maps.size(); ++k)
      suppressNeighborhood(maps[k], bl, bc, minDist, -1.f);
  }
  return out;
}
static void drawCandidates(Mat &dst, const vector<Cand> &cands, const vector<Size> &templSizes, const Cand *best)
{
  for (auto &p : cands)
  {
    Rect roi(p.c - templSizes[p.k].width / 2, p.l - templSizes[p.k].height / 2, templSizes[p.k].width, templSizes[p.k].height);
    rectangle(dst, roi, Scalar(255, 200, 0), 1, LINE_AA);
  }
  if (best)
  {
    Rect roi(best->c - templSizes[best->k].width / 2, best->l - templSizes[best->k].height / 2,
             templSizes[best->k].width, templSizes[best->k].height);
    rectangle(dst, roi, Scalar(0, 255, 255), 2, LINE_AA);
    char text[128];
    std::snprintf(text, sizeof(text), "s=%d CC=%0.2f NCC=%0.2f", best->k, best->cc, best->ncc);
    putText(dst, text, {8, 24}, FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 0), 2, LINE_AA);
    putText(dst, text, {8, 24}, FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 255, 255), 1, LINE_AA);
  }
}

int main(int argc, char *argv[])
{
  if (argc < 2 || argc > 4)
  {
    std::cerr << "uso: cliente4 servidorIp [videosaida.avi] [t/c]\n";
    return 1;
  }
  const char *ip = argv[1];
  const char *outName = (argc >= 3 ? argv[2] : nullptr);
  char mode = (argc == 4 ? argv[3][0] : 't'); // 't' = grava tela; 'c' = só camera

#ifdef _OPENMP
  cv::setNumThreads(1); // evita competição com paralelismo interno do OpenCV
#endif

  CLIENT c(ip);
  cv::namedWindow("cliente4", cv::WINDOW_AUTOSIZE);
  cv::setMouseCallback("cliente4", on_mouse);

  {
    BYTE start = '0';
    c.sendBytes(1, &start);
  }

  // ---------- Carrega modelo ----------
  Mat_<COR> tempColor = imread("include/quadrado.png", 1); // ajuste o caminho se necessário
  if (tempColor.total() == 0)
    erro("Erro: nao encontrou quadrado.png");
  Mat_<FLT> Tfloat;
  converte(tempColor, Tfloat);

  const int NS = 10;
  double s_max = 69.0 / Tfloat.cols, s_min = 19.0 / Tfloat.cols;
  vector<double> S = geoScales(s_min, s_max, NS);
  vector<Mat_<FLT>> Tcc(NS), Tncc(NS);
  vector<Size> Tsize(NS);

#pragma omp parallel for schedule(static)
  for (int i = 0; i < NS; ++i)
  {
    Mat_<FLT> Tr;
    resize(Tfloat, Tr, Size(), S[i], S[i], INTER_NEAREST);
    Tsize[i] = Tr.size();
    Tcc[i] = somaAbsDois(dcReject(Tr, 1.0f));
    Tncc[i] = Tr.clone();
  }

  cv::VideoWriter wr;
  bool wrOpen = false;
  int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
  double fpsHint = 18.0;

  // ---------- Parâmetros ----------
  const float THRESH_NCC = 0.55f; // confiança
  const float FRAC_MIN = 0.06f;   // fração de largura mínima típica
  const float FRAC_MAX = 0.20f;   // fração de largura máxima típica

  // Velocidade de avanço diminui com o tamanho do alvo
  const int V_FWD_MIN = 20; // 0..100
  const int V_FWD_MAX = 80; // 0..100

  // ganho de esterço adaptativo
  const float K_STEER_MIN = 20.0f; // alvo grande → giro suave
  const float K_STEER_MAX = 40.0f; // alvo pequeno → giro agressivo

  auto clamp01 = [](float v)
  { return std::max(0.f, std::min(1.f, v)); };
  auto clamp100 = [](int v)
  { return std::max(-100, std::min(100, v)); };

  // filtros EMA para suavizar saídas (mais lerdos quando alvo grande)
  static float lFilt = 0.0f, rFilt = 0.0f;

  BYTE out = '0';
  int frames = 0;
  double t1 = nowSec();

  while (true)
  {
    Mat_<COR> cam;
    c.receiveImgComp(cam);
    g_cols = cam.cols;
    g_rows = cam.rows;

    // ---------- Localização ----------
    Mat_<FLT> f;
    converte(cam, f);
    vector<Mat_<float>> Rcc(NS), Rncc(NS);

#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < NS; ++i)
      Rcc[i] = matchTemplateSame(f, Tcc[i], TM_CCORR, 0.0f);

    vector<Cand> cands = topKWithSeparation(Rcc, Tsize, /*K=*/20, /*minDist=*/10);

#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < NS; ++i)
      Rncc[i] = matchTemplateSame(f, Tncc[i], TM_CCOEFF_NORMED, 0.0f);

    Cand best;
    bool found = false;
    for (auto &p : cands)
    {
      float v = Rncc[p.k](p.l, p.c);
      p.ncc = v;
      if (!found || v > best.ncc)
      {
        best = p;
        found = true;
      }
    }

    // ---------- Decide controle contínuo ----------
    char cmd_key_or_auto = '0';
    int8_t L = 0, R = 0; // potências analógicas -100..+100
    Mat telaCam = cam.clone();

    if (found && best.ncc >= THRESH_NCC)
    {
      const float cx = cam.cols * 0.5f;
      float ex = (best.c - cx) / float(cam.cols);         // erro lateral [-0.5,0.5]
      float frac = Tsize[best.k].width / float(cam.cols); // tamanho relativo [0..1]

      // Escalar [0..1] do tamanho: 0=pequeno (longe), 1=grande (perto)
      float alpha = clamp01((frac - FRAC_MIN) / (FRAC_MAX - FRAC_MIN));

      // 1) velocidade de avanço decresce com o tamanho
      float vbase = (1.0f - alpha) * V_FWD_MAX + alpha * V_FWD_MIN;

      // 2) ganho de esterço decresce com o tamanho
      float K_STEER_GAIN = (1.0f - alpha) * K_STEER_MAX + alpha * K_STEER_MIN;
      float steer = K_STEER_GAIN * ex; // proporcional puro

      // 3) compor L/R (sem ré automática por padrão)
      float lf = vbase + steer;
      float rf = vbase - steer;

      int li = clamp100((int)std::round(lf));
      int ri = clamp100((int)std::round(rf));

      li = std::max(0, li);
      ri = std::max(0, ri);

      // Garante não-negatividade e faixa segura
      li = clamp100((int)std::round(li));
      ri = clamp100((int)std::round(ri));

      printf("DEBUG: ex=%.3f alpha=%.3f v=%.1f K=%.1f L=%d R=%d\n",
             ex, alpha, vbase, K_STEER_GAIN, li, ri);

      // 4) suavização EMA dependente do tamanho
      // beta = fração "nova". Pequeno (longe) → mais responsivo; Grande (perto) → mais amortecido.
      float beta_small = 0.50f; // responsivo
      float beta_big = 0.15f;   // suave
      float beta = (1.0f - alpha) * beta_small + alpha * beta_big;

      lFilt = (1.0f - beta) * lFilt + beta * li;
      rFilt = (1.0f - beta) * rFilt + beta * ri;

      L = (int8_t)clamp100((int)std::round(lFilt));
      R = (int8_t)clamp100((int)std::round(rFilt));

      drawCandidates(telaCam, cands, Tsize, &best);
      cv::circle(telaCam, cv::Point(best.c, best.l), 3, cv::Scalar(0, 255, 0), cv::FILLED, cv::LINE_AA);

      char hud[180];
      std::snprintf(hud, sizeof(hud),
                    "NCC=%.2f ex=%+.3f size=%.3f v=%.0f K=%.0f beta=%.2f L=%d R=%d",
                    best.ncc, ex, frac, vbase, K_STEER_GAIN, beta, (int)L, (int)R);
      cv::putText(telaCam, hud, {8, 44}, cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 0, 0), 2, cv::LINE_AA);
      cv::putText(telaCam, hud, {8, 44}, cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
    }
    else
    {
      drawCandidates(telaCam, cands, Tsize, nullptr);
      // solta suavemente até parar
      float beta_idle = 0.15f;
      lFilt = (1.0f - beta_idle) * lFilt;
      rFilt = (1.0f - beta_idle) * rFilt;
      L = (int8_t)clamp100((int)std::round(lFilt));
      R = (int8_t)clamp100((int)std::round(rFilt));
    }

    // ---------- Teclado (override) ----------
    bool overrideOn = (g_pressed >= 1 && g_pressed <= 9);
    if (overrideOn)
    {
      cmd_key_or_auto = char('0' + g_pressed); // compatível c/ servidor antigo
    }

    // ---------- Montagem da tela (teclado | camera) e gravação ----------
    cv::Mat kb = makeKeyboard(cam.cols, cam.rows, overrideOn ? g_pressed : 0);
    cv::Mat view;
    cv::hconcat(kb, (mode == 'c' ? cv::Mat(cam) : telaCam), view);
    cv::imshow("cliente4", view);

    if (outName && !wrOpen)
    {
      wr.open(outName, fourcc, fpsHint, view.size(), true);
      if (!wr.isOpened())
        erro("Falha ao abrir VideoWriter");
      wrOpen = true;
    }
    if (wrOpen)
      wr << view;

    int ch = cv::waitKey(1) & 0xFF;
    if (ch == 27)
    {
      BYTE bye = 's';
      c.sendBytes(1, &bye);
      break;
    }

    // ---------- Envio ----------
    if (overrideOn)
    {
      BYTE b = (BYTE)cmd_key_or_auto;
      c.sendBytes(1, &b);
    }
    else
    {
      BYTE pkt[3];
      pkt[0] = 'A';
      pkt[1] = (BYTE)L;
      pkt[2] = (BYTE)R;
      c.sendBytes(3, pkt);
    }

    frames++;
  }

  if (wrOpen)
    wr.release();
  double dt = nowSec() - t1;
  if (dt > 0)
    std::printf("Quadros=%d tempo=%.2fs fps=%.2f\n", frames, dt, frames / dt);
  return 0;
}
