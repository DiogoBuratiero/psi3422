// fase3p.cpp — versão paralela (OpenMP) de fase3.cpp
// Lição de casa 1 da Aula 4: paralelizar os matchTemplate (CC e NCC) nas ~10 escalas
// Compilar (sequencial):
//   g++ -std=c++17 fase3p.cpp -o fase3p `pkg-config --cflags --libs opencv4`
// Compilar (paralelo OpenMP):
//   g++ -std=c++17 fase3p.cpp -o fase3p `pkg-config --cflags --libs opencv4` -fopenmp
// Executar:  ./fase3p capturado.avi quadrado.png localiza.avi

#include "projeto.hpp"
#include <opencv2/opencv.hpp>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cmath>
#ifdef _OPENMP
  #include <omp.h>
#endif

using namespace cv;
using std::vector;

// ---------- util: tempo em segundos ----------
static inline double nowSec() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// ---------- gera escalas geométricas entre [min,max] em N passos ----------
static vector<double> geoScales(double s_min, double s_max, int N) {
  vector<double> s(N);
  double g = std::pow(s_max / s_min, 1.0 / (N - 1));
  double v = s_min;
  for (int i = 0; i < N; ++i) { s[i] = v; v *= g; }
  return s;
}

// ---------- empacota um candidato ----------
struct Cand {
  int l = 0, c = 0;  // posição (linha, coluna)
  int k = 0;         // índice de escala
  float cc = -1.0f;  // correlação CC
  float ncc = -1.0f; // correlação NCC (após validação)
};

// ---------- mascara um “disco” de raio r em volta de (l,c) ----------
static void suppressNeighborhood(Mat_<float> &R, int l, int c, int r, float val = 0.0f) {
  int L = std::max(0, l - r), Rl = std::min(R.rows - 1, l + r);
  int C = std::max(0, c - r), Cr = std::min(R.cols - 1, c + r);
  for (int y = L; y <= Rl; ++y) {
    for (int x = C; x <= Cr; ++x) {
      int dy = y - l, dx = x - c;
      if (dy * dy + dx * dx <= r * r) R(y, x) = val;
    }
  }
}

// ---------- non-max suppression “global”: pega no máx. K picos separados por ≥dist ----------
static vector<Cand> topKWithSeparation(const vector<Mat_<float>> &ccMaps,
                                       const vector<Size> &templSizes,
                                       int K, int minDist) {
  vector<Cand> out;
  vector<Mat_<float>> maps;
  maps.reserve(ccMaps.size());
  for (const auto &m : ccMaps) maps.push_back(m.clone());

  for (int t = 0; t < K; ++t) {
    float best = -1.0f; int bk = -1, bl = -1, bc = -1;
    for (int k = 0; k < (int)maps.size(); ++k) {
      double minv, maxv; Point minp, maxp;
      minMaxLoc(maps[k], &minv, &maxv, &minp, &maxp);
      if (maxv > best) { best = (float)maxv; bk = k; bl = maxp.y; bc = maxp.x; }
    }
    if (bk < 0) break;
    out.push_back({bl, bc, bk, best, -1.0f});
    for (int k = 0; k < (int)maps.size(); ++k)
      suppressNeighborhood(maps[k], bl, bc, minDist, -1.0f);
  }
  return out;
}

// ---------- desenha candidatos/selecionado ----------
static void drawCandidates(Mat &dst, const vector<Cand> &cands,
                           const vector<Size> &templSizes, const Cand *best) {
  for (auto &p : cands) {
    Rect roi(p.c - templSizes[p.k].width / 2, p.l - templSizes[p.k].height / 2,
             templSizes[p.k].width, templSizes[p.k].height);
    rectangle(dst, roi, Scalar(255, 200, 0), 1, LINE_AA); // ciano/azul claro
  }
  if (best) {
    Rect roi(best->c - templSizes[best->k].width / 2, best->l - templSizes[best->k].height / 2,
             templSizes[best->k].width, templSizes[best->k].height);
    rectangle(dst, roi, Scalar(0, 255, 255), 2, LINE_AA); // amarelo
    char text[128];
    std::snprintf(text, sizeof(text), "s=%d  CC=%.2f  NCC=%.2f", best->k, best->cc, best->ncc);
    putText(dst, text, Point(8, 24), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 0), 2, LINE_AA);
    putText(dst, text, Point(8, 24), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 255, 255), 1, LINE_AA);
  }
}

// ---------- barra de progresso no terminal ----------
static void printProgressBar(int done, int total, int width = 40) {
  if (total <= 0) return;
  double frac = std::max(0, std::min(done, total)) / (double)total;
  int filled = (int)std::round(frac * width);
  std::fprintf(stderr, "\r[");
  for (int i = 0; i < width; ++i) std::fputc(i < filled ? '#' : '-', stderr);
  std::fprintf(stderr, "] %3.0f%%  (%d/%d)", frac * 100.0, done, total);
  std::fflush(stderr);
}

// ---------- spinner quando total é desconhecido ----------
static void printSpinner(int done) {
  static const char glyphs[] = {'|', '/', '-', '\\'};
  static int idx = 0;
  std::fprintf(stderr, "\r%c  frames=%d", glyphs[idx], done);
  std::fflush(stderr);
  idx = (idx + 1) & 3;
}

int main(int argc, char **argv) try {
  if (argc != 4) {
    std::fprintf(stderr, "uso: %s capturado.avi quadrado.png localiza.avi\n", argv[0]);
    return 1;
  }
  const char *vin = argv[1];
  const char *tpath = argv[2];
  const char *vout = argv[3];

  cv::setNumThreads(1);

#ifdef _OPENMP
#endif

  // ---- abre vídeo 240x320 (entrada) e prepara saída ----
  int nl = 240, nc = 320;
  VideoCapture vi(vin);
  if (!vi.isOpened()) erro("Erro: Abertura de video");

  int totalFrames = (int)std::round(vi.get(CAP_PROP_FRAME_COUNT));
  if (totalFrames < 0) totalFrames = 0;

  VideoWriter vo(vout, cv::VideoWriter::fourcc('X','V','I','D'), 20, Size(nc, nl));
  if (!vo.isOpened()) erro("Erro: abertura de VideoWriter para saída");

  // ---- lê modelo e prepara “don’t care” (pixels 1.0) + somaAbsDois ----
  Mat_<COR> tempColor = imread(tpath, 1);
  if (tempColor.total() == 0) erro("Erro leitura do modelo (quadrado.png)");
  Mat_<FLT> Tfloat; converte(tempColor, Tfloat); // BGR->cinza float [0..1]

  // ---- 10 escalas geométricas (ex.: 69→19 px como na apostila) ----
  const int NS = 10;
  double s_max = 69.0 / Tfloat.cols;
  double s_min = 19.0 / Tfloat.cols;
  vector<double> S = geoScales(s_min, s_max, NS);

  vector<Mat_<FLT>> Tcc(NS), Tncc(NS);
  vector<Size> Tsize(NS);

  // Pré-processamento de modelos por escala — Paralelo
  // Cada iteração usa apenas variáveis locais/cópias e escreve em índices distintos (thread-safe)
  #pragma omp parallel for if(NS>1)
  for (int i = 0; i < NS; ++i) {
    Mat_<FLT> Tr; // local a cada thread
    resize(Tfloat, Tr, Size(), S[i], S[i], INTER_NEAREST);
    Tsize[i] = Tr.size();
    Tcc[i] = somaAbsDois(dcReject(Tr, 1.0f)); // CC com “don’t care”
    Tncc[i] = Tr.clone();                     // NCC
  }

  // buffers por frame
  Mat_<COR> a, out;
  Mat_<FLT> f;
  vector<Mat_<float>> Rcc(NS), Rncc(NS);

  int frames = 0;
  double t1 = nowSec();

  while (true) {
    vi >> a; if (!a.data) break;

    if (a.rows != nl || a.cols != nc) {
      Mat tmp; resize(a, tmp, Size(nc, nl), 0, 0, INTER_AREA); tmp.copyTo(a);
    }

    // converte para float cinza
    converte(a, f);

    // (1) CC em todas as escalas (modo SAME) — Paralelo
    #pragma omp parallel for if(NS>1)
    for (int i = 0; i < NS; ++i) {
      Rcc[i] = matchTemplateSame(f, Tcc[i], TM_CCORR, 0.0f);
    }

    // (2) top-20 picos CC
    vector<Cand> cands = topKWithSeparation(Rcc, Tsize, /*K=*/20, /*minDist=*/10);

    // (3) NCC apenas nas escalas presentes entre os 20 candidatos (como na apostila)
// Em vez de calcular NCC para TODAS as escalas, calculamos só para as que tiveram pico em CC.
std::vector<char> need(NS, 0);
for (auto &p : cands) need[p.k] = 1;

#pragma omp parallel for if(NS>1)
for (int i = 0; i < NS; ++i) {
  if (!need[i]) continue;
  Rncc[i] = matchTemplateSame(f, Tncc[i], TM_CCOEFF_NORMED, 0.0f);
}

    // Seleciona melhor candidato pela maior NCC nas posições candidatas
    Cand best; bool found = false;
    for (auto &p : cands) {
      float v = Rncc[p.k](p.l, p.c);
      p.ncc = v;
      if (!found || v > best.ncc) { best = p; found = true; }
    }

    // (4) decisão (threshold NCC)
    const float THRESH_NCC = 0.55f;
    out = a.clone();
    if (found && best.ncc >= THRESH_NCC) drawCandidates(out, cands, Tsize, &best);
    else                                 drawCandidates(out, cands, Tsize, nullptr);

    vo << out;
    frames++;

    // ---- Barra de progresso / Spinner ----
    if (totalFrames > 0) printProgressBar(frames, totalFrames); else printSpinner(frames);
  }

  double t2 = nowSec();
  double dt = std::max(1e-9, t2 - t1);

  // Finaliza linha da barra e imprime FPS
  if (totalFrames > 0) { std::fprintf(stderr, "\r"); printProgressBar(totalFrames, totalFrames); }
  std::fprintf(stderr, "\n");
  std::printf("Processados %d quadros em %.3fs  →  FPS = %.2f\n", frames, dt, frames / dt);

  return 0;
}
catch (const std::exception &e) {
  std::fprintf(stderr, "Excecao: %s\n", e.what());
  return 1;
}
catch (...) {
  std::fprintf(stderr, "Excecao desconhecida\n");
  return 1;
}
