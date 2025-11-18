// cliente6.cpp — Localiza placa (fase5) + lê dígito e roda máquina de estados local.
// Uso:   ./cliente6 <ip_raspberry> [videosaida.avi] [t/c]
// Comp.: g++ -std=c++17 cliente6.cpp -o cliente6 `pkg-config --cflags --libs opencv4` -fopenmp

#include "projeto.hpp"
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <experimental/optional>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

using std::experimental::optional;
using std::experimental::nullopt;
using std::string;
using std::vector;
using cv::Mat;
using cv::Mat_;
using cv::Rect;
using cv::Size;
using cv::Vec3b;
using cv::VideoWriter;
using cv::VideoCapture;

// ---------- util: checagem MNIST ----------
static bool fileExists(const string &p)
{
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool ensureMnistFiles(const string &dir)
{
  const vector<string> files = {
      "train-images.idx3-ubyte",
      "train-labels.idx1-ubyte",
      "t10k-images.idx3-ubyte",
      "t10k-labels.idx1-ubyte"};
  bool ok = true;
  for (const auto &f : files)
  {
    if (!fileExists(dir + "/" + f))
    {
      std::cerr << "MNIST faltando: " << dir << "/" << f << "\n";
      ok = false;
    }
  }
  return ok;
}

// ---------- util: tempo em segundos ----------
static inline double nowSec()
{
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// ---------- localização da placa (fase5 simplificado do cliente4) ----------
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

// ---------- detecção do dígito na ROI localizada ----------
struct DigitDetection
{
  int value = -1;
  Rect bbox;
  double score = 0.0;
};

static optional<DigitDetection> detectDigit(const Mat_<COR> &roi, const Rect &roiRect, MnistFlann &mnist, bool mnistReady)
{
  if (!mnistReady)
    return nullopt;

  Mat gray;
  cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
  cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

  Mat bin;
  cv::adaptiveThreshold(gray, bin, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY_INV, 25, 7);
  cv::morphologyEx(bin, bin, cv::MORPH_OPEN, cv::Mat::ones(3, 3, CV_8U));

  vector<vector<cv::Point>> contours;
  cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  const double minArea = roi.total() * 0.01; // ROI já isolada
  const double maxArea = roi.total() * 0.85;
  const double minRatio = 0.35, maxRatio = 1.6;

  DigitDetection best;
  bool found = false;

  for (auto &cnt : contours)
  {
    Rect bb = cv::boundingRect(cnt);
    double area = (double)bb.area();
    if (area < minArea || area > maxArea)
      continue;
    double ratio = (double)bb.width / (double)bb.height;
    if (ratio < minRatio || ratio > maxRatio)
      continue;

    int pad = std::max(2, std::min(bb.width, bb.height) / 6);
    Rect expanded(bb.x - pad, bb.y - pad, bb.width + 2 * pad, bb.height + 2 * pad);
    expanded &= Rect(0, 0, roi.cols, roi.rows);
    if (expanded.width < 12 || expanded.height < 12)
      continue;

    Mat digitROI = bin(expanded).clone();
    int nz = cv::countNonZero(digitROI);
    if (nz < 20)
      continue;

    Mat resized;
    cv::resize(digitROI, resized, Size(28, 28), 0, 0, cv::INTER_AREA);

    Mat_<FLT> query;
    resized.convertTo(query, CV_32F, 1.0 / 255.0);
    int pred = (int)std::round(mnist.predict(query));

    double fill = (double)cv::countNonZero(resized) / 784.0;
    double score = area * fill;
    if (!found || score > best.score)
    {
      Rect bbGlobal = Rect(roiRect.x + expanded.x, roiRect.y + expanded.y, expanded.width, expanded.height);
      best = {pred, bbGlobal, score};
      found = true;
    }
  }

  if (!found)
    return nullopt;
  return best;
}

// ---------- Estado do "teclado" por cursor (cliente4) ----------
static int g_pressed = 0; // 0 nada; 1..9 enquanto cursor pressionado na célula
static bool g_mouseDown = false;
static int g_cols = 320, g_rows = 240;

// Mapa numpad (teclado à ESQUERDA, câmera à DIREITA)
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

int main(int argc, char *argv[])
{
  if (argc < 2 || argc > 4)
  {
    std::cerr << "uso: cliente6 servidorIp [videosaida.avi] [t/c]\n";
    return 1;
  }
  const char *ip = argv[1];
  const char *outName = (argc >= 3 ? argv[2] : nullptr);
  char mode = (argc == 4 ? argv[3][0] : 't'); // 't' = grava tela; 'c' = só camera

#ifdef _OPENMP
  cv::setNumThreads(1); // evita competição com paralelismo interno do OpenCV
#endif

  CLIENT c(ip);

  // ---------- Inicializa MNIST ----------
  const string mnistDir = "include/mnist";
  MnistFlann mnist(28, true, true, "flann");
  bool mnistReady = false;
  try
  {
    if (!ensureMnistFiles(mnistDir))
      throw std::runtime_error("Arquivos MNIST ausentes ou download falhou");
    mnist.le(mnistDir, 60000, 0); // só base de treino
    mnist.train();
    mnistReady = true;
    std::cerr << "MNIST carregado e indexado (FLANN KDTree)\n";
  }
  catch (const std::exception &e)
  {
    std::cerr << "Falha ao carregar/treinar MNIST: " << e.what()
              << "\nDetecção automática ficará desabilitada; use o teclado (0-9).\n";
  }

  cv::namedWindow("cliente6", cv::WINDOW_AUTOSIZE);
  cv::setMouseCallback("cliente6", on_mouse);

  // manda parada inicial para sincronizar
  {
    BYTE start = '0';
    c.sendBytes(1, &start);
  }

  // ---------- Carrega modelo do quadrado (fase5) ----------
  Mat_<COR> tempColor = imread("include/quadrado.png", 1); // mesmo caminho do cliente4
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

  VideoWriter wr;
  bool wrOpen = false;
  int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
  double fpsHint = 18.0;

  // ---------- Parâmetros ----------
  const float THRESH_NCC = 0.55f; // confiança da localização
  const int STABLE_FRAMES = 3;
  const double DUR_FORWARD = 1.0;
  const double DUR_TURN_90 = 0.65;
  const double DUR_TURN_180 = 1.25;

  int frames = 0;
  double t1 = nowSec();

  int lastPred = -1, stableCount = 0, missFrames = 0;
  char activeCmd = '0';
  double cmdUntil = 0.0;
  auto commandDuration = [&](char cmd) -> double
  {
    switch (cmd)
    {
    case '4':
    case '5':
      return DUR_FORWARD;
    case '6':
    case '7':
    case '8':
    case '9':
      return DUR_TURN_90;
    case '2':
    case '3':
      return DUR_TURN_180;
    default:
      return 0.0;
    }
  };

  while (true)
  {
    Mat_<COR> cam;
    c.receiveImgComp(cam);
    g_cols = cam.cols;
    g_rows = cam.rows;

    // ---------- Localização da placa ----------
    Mat_<FLT> f;
    converte(cam, f);
    vector<Mat_<float>> Rcc(NS), Rncc(NS);
#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < NS; ++i)
    {
      Rcc[i] = matchTemplateSame(f, Tcc[i], cv::TM_CCORR);
      Rncc[i] = matchTemplateSame(f, Tncc[i], cv::TM_CCOEFF_NORMED);
    }
    vector<Cand> cands = topKWithSeparation(Rcc, Tsize, 5, 10);
    for (auto &c : cands)
      c.ncc = Rncc[c.k](c.l, c.c);

    Cand best;
    bool found = false;
    for (auto &c : cands)
      if (!found || c.ncc > best.ncc)
      {
        best = c;
        found = true;
      }

    optional<DigitDetection> det = nullopt;
    if (found && best.ncc >= THRESH_NCC)
    {
      Rect roi(best.c - Tsize[best.k].width / 2,
               best.l - Tsize[best.k].height / 2,
               Tsize[best.k].width, Tsize[best.k].height);
      roi &= Rect(0, 0, cam.cols, cam.rows);
      Mat_<COR> roiImg = cam(roi).clone();
      det = detectDigit(roiImg, roi, mnist, mnistReady);
    }

    int pred = det ? det->value : -1;
    if (det)
    {
      if (pred == lastPred)
        stableCount++;
      else
      {
        stableCount = 1;
        lastPred = pred;
      }
      missFrames = 0;
    }
    else
    {
      stableCount = 0;
      lastPred = -1;
      missFrames++;
    }

    // ---------- Máquina de estados local (mesma semântica dos dígitos) ----------
    double now = nowSec();
    if (now >= cmdUntil && activeCmd != '0')
    {
      BYTE stop = '0';
      c.sendBytes(1, &stop);
      activeCmd = '0';
    }

    char nextCmd = 0;
    double nextDur = 0.0;
    bool manualOverride = false;
    if (g_pressed >= 1 && g_pressed <= 9)
    {
      nextCmd = char('0' + g_pressed);
      nextDur = commandDuration(nextCmd);
      manualOverride = true;
    }
    else if (det && stableCount >= STABLE_FRAMES && pred != -1 && pred != (activeCmd - '0'))
    {
      nextCmd = char('0' + pred);
      nextDur = commandDuration(nextCmd);
    }
    else if (!det && missFrames > 20 && activeCmd != '0')
    {
      BYTE stop = '0';
      c.sendBytes(1, &stop);
      activeCmd = '0';
    }

    if (nextCmd)
    {
      BYTE b = (BYTE)nextCmd;
      c.sendBytes(1, &b);
      activeCmd = nextCmd;
      cmdUntil = now + nextDur;
      std::cerr << (manualOverride ? "Manual" : "Auto") << " cmd=" << nextCmd << " dur=" << nextDur << "s\n";
    }

    // ---------- HUD ----------
    Mat tela = cam.clone();
    if (found)
    {
      Rect roi(best.c - Tsize[best.k].width / 2,
               best.l - Tsize[best.k].height / 2,
               Tsize[best.k].width, Tsize[best.k].height);
      roi &= Rect(0, 0, cam.cols, cam.rows);
      cv::rectangle(tela, roi, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    }
    if (det)
    {
      cv::rectangle(tela, det->bbox, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
      char lbl[32];
      std::snprintf(lbl, sizeof(lbl), "dig=%d", det->value);
      cv::putText(tela, lbl, {det->bbox.x, std::max(20, det->bbox.y - 6)}, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 0), 2, cv::LINE_AA);
      cv::putText(tela, lbl, {det->bbox.x, std::max(20, det->bbox.y - 6)}, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
    }
    {
      char hud[200];
      std::snprintf(hud, sizeof(hud),
                    "MNIST:%s pred=%d stab=%d miss=%d cmd=%c tleft=%.2f",
                    mnistReady ? "on" : "off",
                    pred, stableCount, missFrames, activeCmd, std::max(0.0, cmdUntil - now));
      cv::putText(tela, hud, {8, 24}, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 0), 2, cv::LINE_AA);
      cv::putText(tela, hud, {8, 24}, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
    }

    cv::Mat kb = makeKeyboard(cam.cols, cam.rows, (g_pressed >= 1 && g_pressed <= 9) ? g_pressed : 0);
    cv::Mat view;
    cv::hconcat(kb, (mode == 'c' ? Mat(cam) : tela), view);
    cv::imshow("cliente6", view);

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
    if (ch == 27) // ESC
    {
      BYTE bye = 's';
      c.sendBytes(1, &bye);
      break;
    }
    if ('0' <= ch && ch <= '9')
    {
      BYTE b = (BYTE)ch;
      c.sendBytes(1, &b);
      activeCmd = ch;
      cmdUntil = nowSec() + commandDuration(ch);
      std::cerr << "Manual cmd=" << ch << "\n";
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
