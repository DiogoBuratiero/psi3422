// fase5.cpp — Localiza placa + reconhece dígito manuscrito (MNIST + FLANN)
// Compilar (no laboratório):  compila fase5 -ocv -v3 -omp
// Executar:                  ./fase5 capturado.avi quadrado.png locarec.avi

#include "projeto.hpp"
#include <opencv2/opencv.hpp>
#include <opencv2/flann.hpp>
#include <chrono>
#include <array>
#include <cmath>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace cv;
using std::vector;

// util: tempo em segundos
static inline double nowSec()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// gera escalas geométricas entre [min,max] em N passos
static vector<double> geoScales(double s_min, double s_max, int N)
{
    vector<double> s(N);
    double g = std::pow(s_max / s_min, 1.0 / (N - 1));
    double v = s_min;
    for (int i = 0; i < N; ++i)
    {
        s[i] = v;
        v *= g;
    }
    return s;
}

// empacota um candidato
struct Cand
{
    int l = 0, c = 0;  // posição (linha, coluna)
    int k = 0;         // índice de escala
    float cc = -1.0f;  // correlação CC
    float ncc = -1.0f; // correlação NCC (após validação)
};

// mascara um disco de raio r
static void suppressNeighborhood(Mat_<float> &R, int l, int c, int r, float val = 0.0f)
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

// pega no máx. K picos
static vector<Cand> topKWithSeparation(const vector<Mat_<float>> &ccMaps,
                                       const vector<Size> &templSizes,
                                       int K, int minDist)
{
    vector<Cand> out;
    vector<Mat_<float>> maps;
    maps.reserve(ccMaps.size());
    for (const auto &m : ccMaps)
        maps.push_back(m.clone());

    for (int t = 0; t < K; ++t)
    {
        float best = -1.0f;
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
        out.push_back({bl, bc, bk, best, -1.0f});
        for (int k = 0; k < (int)maps.size(); ++k)
            suppressNeighborhood(maps[k], bl, bc, minDist, -1.0f);
    }
    return out;
}

// desenha retângulos das placas
static void drawCandidates(Mat &dst, const vector<Cand> &cands,
                           const vector<Size> &templSizes, const Cand *best)
{
    for (auto &p : cands)
    {
        Rect roi(p.c - templSizes[p.k].width / 2,
                 p.l - templSizes[p.k].height / 2,
                 templSizes[p.k].width, templSizes[p.k].height);
        rectangle(dst, roi, Scalar(255, 200, 0), 1, LINE_AA);
    }
    if (best)
    {
        Rect roi(best->c - templSizes[best->k].width / 2,
                 best->l - templSizes[best->k].height / 2,
                 templSizes[best->k].width, templSizes[best->k].height);
        rectangle(dst, roi, Scalar(0, 255, 255), 2, LINE_AA);
        char text[128];
        std::snprintf(text, sizeof(text),
                      "s=%d  CC=%.2f  NCC=%.2f", best->k, best->cc, best->ncc);
        putText(dst, text, Point(8, 24),
                FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 0), 2, LINE_AA);
        putText(dst, text, Point(8, 24),
                FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 255, 255), 1, LINE_AA);
    }
}

// ---------- barra de progresso no terminal ----------
static void printProgressBar(int done, int total, int width = 40)
{
    if (total <= 0)
        return;
    double frac = std::max(0, std::min(done, total)) / (double)total;
    int filled = (int)std::round(frac * width);
    std::fprintf(stderr, "\r[");
    for (int i = 0; i < width; ++i)
        std::fputc(i < filled ? '#' : '-', stderr);
    std::fprintf(stderr, "] %3.0f%%  (%d/%d)", frac * 100.0, done, total);
    std::fflush(stderr);
}

// ---------------------------------------------------------------------
// Classificador de dígitos com FLANN (k-NN)

struct DigitFlann
{
    Mat trainX; // na × 196, CV_32F
    Mat trainY; // na × 1, CV_32S
    cv::flann::Index index;

    DigitFlann(MNIST &mnist)
        : trainX(mnist.ax.clone()),
          trainY(mnist.ay.clone()),
          index(trainX, cv::flann::KDTreeIndexParams(4))
    {
        if (trainX.type() != CV_32F)
            trainX.convertTo(trainX, CV_32F);
        if (trainY.type() != CV_32S)
            trainY.convertTo(trainY, CV_32S);
    }

    int predict(const Mat &feat, int k = 5)
    {
        CV_Assert(feat.rows == 1);
        Mat query;
        if (feat.type() != CV_32F)
            feat.convertTo(query, CV_32F);
        else
            query = feat;

        Mat indices, dists;
        index.knnSearch(query, indices, dists, k, cv::flann::SearchParams());

        std::array<int, 10> votes{};
        for (int i = 0; i < k; ++i)
        {
            int idx = indices.at<int>(0, i);
            int label = trainY.at<int>(idx, 0);
            if (0 <= label && label <= 9)
                votes[label]++;
        }
        int bestLabel = 0;
        for (int d = 1; d < 10; ++d)
            if (votes[d] > votes[bestLabel])
                bestLabel = d;
        return bestLabel;
    }
};

// ---------------------------------------------------------------------
// Pré-processa ROI do dígito → 1×196 (float, 0..1)
static bool preprocessDigit(const Mat &frameGray, const Rect &plateRect,
                            MNIST &mnist, Mat &digitFeat,
                            Mat &dbgRaw, Mat &dbgProcessed)
{
    // ===== 1. Recorte central =====
    // Usamos um quadrado central para evitar moldura preta
    int side = std::min(plateRect.width, plateRect.height);
    int cx = plateRect.x + plateRect.width  / 2;
    int cy = plateRect.y + plateRect.height / 2;

    // 45% do lado da placa = região bem central
    int innerSide = (int)std::round(0.45 * side);
    Rect inner(cx - innerSide/2, cy - innerSide/2, innerSide, innerSide);
    inner &= Rect(0, 0, frameGray.cols, frameGray.rows);

    if (inner.width < 12 || inner.height < 12)
        return false;

    Mat roi = frameGray(inner).clone(); // GRY
    dbgRaw = roi.clone();

    // ===== 2. Stretch de contraste global =====
    double minv, maxv;
    minMaxLoc(roi, &minv, &maxv);
    if (maxv - minv < 10.0)
        return false;

    Mat stretched;
    roi.convertTo(stretched, CV_8U,
                  255.0 / (maxv - minv),
                  -minv * 255.0 / (maxv - minv));

    // ===== 4. Binarização =====
    Mat bin;
    threshold(stretched, bin, 0, 255, THRESH_BINARY | THRESH_OTSU);

    // ===== 5. BBox MNIST (remove bordas brancas e redimensiona p/ 14x14) =====
    Mat_<GRY> gryBin = bin;
    Mat_<GRY> bboxImg = mnist.bbox(gryBin); // shrink + resize 14x14

    if (bboxImg.empty())
        return false;

    dbgProcessed = bboxImg.clone(); // para overlay no canto superior

    // ===== 6. Converte para float e achata =====
    Mat_<FLT> flt;
    bboxImg.convertTo(flt, CV_32F, 1.0/255.0);

    if (!flt.isContinuous())
        flt = flt.clone();

    Mat feat = flt.reshape(1, 1); // 1×196
    feat.copyTo(digitFeat);

    return true;
}


int main(int argc, char **argv)
try
{
    if (argc != 4)
    {
        std::fprintf(stderr,
                     "uso: %s capturado.avi quadrado.png locarec.avi\n", argv[0]);
        return 1;
    }
    const char *vin = argv[1];
    const char *tpath = argv[2];
    const char *vout = argv[3];

#ifdef _OPENMP
    cv::setNumThreads(1);
#endif

    // --------- 1) Lê MNIST e treina FLANN ----------
    std::printf("Lendo MNIST e treinando FLANN...\n");
    MNIST mnist(14, true, true);
    mnist.le("./include/mnist");
    DigitFlann clf(mnist);
    std::printf("MNIST: %d amostras de treino.\n", mnist.na);

    // --------- 2) Abre vídeo de entrada e saída ----------
    int nl = 240, nc = 320;
    VideoCapture vi(vin);
    if (!vi.isOpened())
        erro("Erro: abertura de video de entrada");

    int totalFrames = (int)std::round(vi.get(CAP_PROP_FRAME_COUNT));

    VideoWriter vo(vout, VideoWriter::fourcc('X', 'V', 'I', 'D'),
                   20, Size(nc, nl));
    if (!vo.isOpened())
        erro("Erro: abertura de video de saída");

    // --------- 3) Lê modelo quadrado e prepara escalas ----------
    Mat_<COR> tempColor = imread(tpath, 1);
    if (tempColor.total() == 0)
        erro("Erro leitura do modelo (quadrado.png)");
    Mat_<FLT> Tfloat;
    converte(tempColor, Tfloat); // BGR->cinza float [0..1]

    // ---- 10 escalas geométricas (ex.: 69→19 px) ----
    const int NS = 10;
    double s_max = 69.0 / Tfloat.cols;
    double s_min = 19.0 / Tfloat.cols;
    vector<double> S = geoScales(s_min, s_max, NS);

    vector<Mat_<FLT>> Tcc(NS), Tncc(NS);
    vector<Size> Tsize(NS);

// PRÉ-PROCESSAMENTO EM PARALELO (independente por escala)
// - resize com INTER_NEAREST (preserva "1.0" do don't care)
// - CC: somaAbsDois(dcReject(Tr, 1.0f))
// - NCC: cópia direta do template escalado
#pragma omp parallel for schedule(static)
    for (int i = 0; i < NS; ++i)
    {
        Mat_<FLT> Tr;
        resize(Tfloat, Tr, Size(), S[i], S[i], INTER_NEAREST);
        Tsize[i] = Tr.size();
        Tcc[i] = somaAbsDois(dcReject(Tr, 1.0f));
        Tncc[i] = Tr.clone();
    }

    // buffers por frame
    Mat_<COR> a, outColor;
    Mat_<GRY> gray;
    Mat_<FLT> f;
    vector<Mat_<float>> Rcc(NS), Rncc(NS);

    const float THRESH_NCC = 0.55f;
    const float FRAC_NEAR = 0.14f; // placa suficientemente grande

    int frames = 0;
    double t1 = nowSec();

    while (true)
    {
        vi >> a;
        if (!a.data)
            break;

        if (a.rows != nl || a.cols != nc)
        {
            Mat tmp;
            resize(a, tmp, Size(nc, nl), 0, 0, INTER_AREA);
            tmp.copyTo(a);
        }

        converte(a, f);                 // float [0,1] para matching
        cvtColor(a, gray, CV_BGR2GRAY); // ou COLOR_BGR2GRAY dependendo da versão

// (1) CC em todas as escalas (modo SAME) — PARALELO
#pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < NS; ++i)
            Rcc[i] = matchTemplateSame(f, Tcc[i], TM_CCORR, 0.0f);

        // (2) top-20 picos CC (serial — depende dos mapas completos)
        vector<Cand> cands = topKWithSeparation(Rcc, Tsize, /*K=*/20, /*minDist=*/10);

// (3) NCC nas mesmas escalas (modo SAME) — PARALELO
#pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < NS; ++i)
            Rncc[i] = matchTemplateSame(f, Tncc[i], TM_CCOEFF_NORMED, 0.0f);

        // Seleção do melhor candidato via NCC (serial — redução simples)
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

        outColor = a.clone();
        Mat dbgRaw, dbgProc;

        if (found && best.ncc >= THRESH_NCC)
        {
            drawCandidates(outColor, cands, Tsize, &best);

            // retângulo da placa
            Rect plateRect(best.c - Tsize[best.k].width / 2,
                           best.l - Tsize[best.k].height / 2,
                           Tsize[best.k].width,
                           Tsize[best.k].height);
            plateRect &= Rect(0, 0, nc, nl);

            float frac = Tsize[best.k].width / float(nc);

            // se estiver suficientemente perto, tenta ler o dígito
      if (plateRect.width > 0 && plateRect.height > 0 &&
          frac >= FRAC_NEAR) {
        Mat feat;
        if (preprocessDigit(gray, plateRect, mnist, feat,
                            dbgRaw, dbgProc)) {
          int digito = clf.predict(feat, 5);

          // escreve dígito em vermelho perto da placa
          char txt[32];
          std::snprintf(txt, sizeof(txt), "%d", digito);
          Point org(plateRect.x + plateRect.width + 5,
                    plateRect.y + plateRect.height / 2);
          putText(outColor, txt, org,
                  FONT_HERSHEY_SIMPLEX, 2.0,
                  Scalar(0, 0, 255), 3, LINE_AA);

          // ----- subimagem bruta (canto inferior direito) -----
          if (!dbgRaw.empty()) {
            Rect rDst(outColor.cols - 60,
                      outColor.rows - 60, 60, 60);
            Mat dstROI = outColor(rDst);   // COR (3 canais)
            Mat tmpGray, tmpColor;
            resize(dbgRaw, tmpGray, rDst.size(), 0, 0, INTER_AREA);
            cvtColor(tmpGray, tmpColor, COLOR_GRAY2BGR);
            tmpColor.copyTo(dstROI);
          }

          // ----- subimagem pós-processada (canto superior direito) -----
          if (!dbgProc.empty()) {
            Rect rDst(outColor.cols - 60, 0, 60, 60);
            Mat dstROI = outColor(rDst);   // COR (3 canais)
            Mat tmpGray, tmpColor;
            resize(dbgProc, tmpGray, rDst.size(), 0, 0, INTER_NEAREST);
            cvtColor(tmpGray, tmpColor, COLOR_GRAY2BGR);
            tmpColor.copyTo(dstROI);
          }
        }
      }

        }
        else
        {
            drawCandidates(outColor, cands, Tsize, nullptr);
        }

        vo << outColor;
        frames++;

        printProgressBar(frames, totalFrames);
    }

    double t2 = nowSec();
    double dt = std::max(1e-9, t2 - t1);
    std::printf("Processados %d quadros em %.3fs  -> FPS=%.2f\n",
                frames, dt, frames / dt);

    return 0;
}
catch (const std::exception &e)
{
    std::fprintf(stderr, "Exceção: %s\n", e.what());
    return 1;
}
catch (...)
{
    std::fprintf(stderr, "Exceção desconhecida\n");
    return 1;
}
