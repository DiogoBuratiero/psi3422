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

// ---------- util: checagem/baixar MNIST ----------
static bool fileExists(const string &p)
{
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Tenta baixar via wget ou curl; retorna true se arquivo final existir
static bool ensureMnistFiles(const string &dir)
{
  struct Item
  {
    string name;
    string url;
  };
  const vector<Item> todo = {
      {"train-images.idx3-ubyte", "http://yann.lecun.com/exdb/mnist/train-images-idx3-ubyte.gz"},
      {"train-labels.idx1-ubyte", "http://yann.lecun.com/exdb/mnist/train-labels-idx1-ubyte.gz"},
      {"t10k-images.idx3-ubyte", "http://yann.lecun.com/exdb/mnist/t10k-images-idx3-ubyte.gz"},
      {"t10k-labels.idx1-ubyte", "http://yann.lecun.com/exdb/mnist/t10k-labels-idx1-ubyte.gz"}};

  bool allOk = true;
  for (const auto &it : todo)
  {
    string path = dir + "/" + it.name;
    if (fileExists(path))
      continue;

    std::cerr << "MNIST: baixando " << it.name << "...\n";
    string gzPath = path + ".gz";
    string cmd = "wget -q -O '" + gzPath + "' '" + it.url +
                 "' || curl -L -s -o '" + gzPath + "' '" + it.url + "'";
    if (std::system(cmd.c_str()) != 0)
    {
      std::cerr << "MNIST: falha ao baixar " << it.name << "\n";
      allOk = false;
      continue;
    }
    if (std::system(("gzip -df '" + gzPath + "'").c_str()) != 0)
    {
      std::cerr << "MNIST: falha ao descompactar " << gzPath << "\n";
      allOk = false;
      continue;
    }
    if (!fileExists(path))
    {
      std::cerr << "MNIST: arquivo " << path << " continua ausente.\n";
      allOk = false;
    }
  }
  return allOk;
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
  MnistFlann mnist(28, true, true, "flann");
  bool mnistReady = false;
  try
  {
    if (!ensureMnistFiles("."))
      throw std::runtime_error("Arquivos MNIST ausentes ou download falhou");
    mnist.le(".", 60000, 0); // só base de treino
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

  while (true)
  {
    Mat_<COR> cam;
    c.receiveImgComp(cam);

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

    if (det && stableCount >= STABLE_FRAMES && pred != -1 && pred != (activeCmd - '0'))
    {
      char cmd = char('0' + pred);
      double dur = 0.0;
      switch (cmd)
      {
      case '4':
      case '5':
        dur = DUR_FORWARD;
        break;
      case '6':
      case '7':
      case '8':
      case '9':
        dur = DUR_TURN_90;
        break;
      case '2':
      case '3':
        dur = DUR_TURN_180;
        break;
      default:
        dur = 0.0;
        break;
      }

      BYTE b = (BYTE)cmd;
      c.sendBytes(1, &b);
      activeCmd = cmd;
      cmdUntil = now + dur;
      std::cerr << "Cmd=" << cmd << " dur=" << dur << "s\n";
    }
    else if (!det && missFrames > 20 && activeCmd != '0')
    {
      BYTE stop = '0';
      c.sendBytes(1, &stop);
      activeCmd = '0';
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

    cv::imshow("cliente6", (mode == 'c' ? Mat(cam) : tela));

    if (outName && !wrOpen)
    {
      wr.open(outName, fourcc, fpsHint, (mode == 'c' ? cam.size() : tela.size()), true);
      if (!wr.isOpened())
        erro("Falha ao abrir VideoWriter");
      wrOpen = true;
    }
    if (wrOpen)
      wr << (mode == 'c' ? Mat(cam) : tela);

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
      cmdUntil = nowSec(); // manda manual, deixa parado até próximo
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
