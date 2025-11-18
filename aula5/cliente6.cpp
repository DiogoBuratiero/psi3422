// cliente6.cpp — Controle automático por leitura de dígitos em placas
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

struct DigitDetection
{
  int value = -1;
  Rect bbox;
  double score = 0.0;
};

// Localiza um dígito manuscrito usando limiarização + MNIST (KDTree/FLANN)
static optional<DigitDetection> detectDigit(const Mat_<COR> &frame, MnistFlann &mnist, bool mnistReady)
{
  if (!mnistReady)
    return nullopt;

  Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
  cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

  Mat bin;
  cv::adaptiveThreshold(gray, bin, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY_INV, 25, 7);
  cv::morphologyEx(bin, bin, cv::MORPH_OPEN, cv::Mat::ones(3, 3, CV_8U));

  vector<vector<cv::Point>> contours;
  cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  const double minArea = frame.total() * 0.004;  // descarta ruído
  const double maxArea = frame.total() * 0.55;   // evita pegar a placa inteira
  const double minRatio = 0.35, maxRatio = 1.6;  // bounding box plausível

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
    expanded &= Rect(0, 0, frame.cols, frame.rows);
    if (expanded.width < 12 || expanded.height < 12)
      continue;

    Mat digitROI = bin(expanded).clone();
    int nz = cv::countNonZero(digitROI);
    if (nz < 20)
      continue; // quase vazio

    Mat resized;
    cv::resize(digitROI, resized, Size(28, 28), 0, 0, cv::INTER_AREA);

    Mat_<FLT> query;
    resized.convertTo(query, CV_32F, 1.0 / 255.0);
    int pred = (int)std::round(mnist.predict(query));

    double fill = (double)cv::countNonZero(resized) / 784.0;
    double score = area * fill;
    if (!found || score > best.score)
    {
      best = {pred, expanded, score};
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
    // Espera que os arquivos MNIST estejam no diretório atual (baixados da apostila)
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

  VideoWriter wr;
  bool wrOpen = false;
  int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
  double fpsHint = 18.0;

  int lastPred = -1, stableCount = 0;
  int lastSent = 0; // 0 = parado por padrão
  int missFrames = 0;
  const int STABLE_FRAMES = 3;
  const int MISS_BEFORE_STOP = 20;

  double t1 = nowSec();
  int frames = 0;

  while (true)
  {
    Mat_<COR> cam;
    c.receiveImgComp(cam);

    optional<DigitDetection> det = detectDigit(cam, mnist, mnistReady);
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

    char cmdToSend = 0;
    bool autoTriggered = false;
    if (det && stableCount >= STABLE_FRAMES && pred != lastSent)
    {
      cmdToSend = char('0' + pred);
      autoTriggered = true;
    }
    else if (!det && missFrames > MISS_BEFORE_STOP && lastSent != 0)
    {
      cmdToSend = '0'; // perda do alvo → para por segurança
    }

    Mat tela = cam.clone();
    if (det)
    {
      cv::rectangle(tela, det->bbox, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
      char lbl[32];
      std::snprintf(lbl, sizeof(lbl), "dig=%d", pred);
      cv::putText(tela, lbl, {det->bbox.x, std::max(20, det->bbox.y - 6)}, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 0), 2, cv::LINE_AA);
      cv::putText(tela, lbl, {det->bbox.x, std::max(20, det->bbox.y - 6)}, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
    }

    // HUD
    {
      char hud[160];
      std::snprintf(hud, sizeof(hud),
                    "MNIST:%s pred=%d stab=%d miss=%d last=%d",
                    mnistReady ? "on" : "off",
                    pred, stableCount, missFrames, lastSent);
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
      cmdToSend = (char)ch;
      stableCount = 0;
      autoTriggered = false;
    }

    if (cmdToSend)
    {
      BYTE b = (BYTE)cmdToSend;
      c.sendBytes(1, &b);
      if ('0' <= cmdToSend && cmdToSend <= '9')
        lastSent = cmdToSend - '0';

      std::cerr << "Enviado cmd=" << cmdToSend << (autoTriggered ? " [auto]\n" : " [manual]\n");
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
