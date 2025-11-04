// raspberry.hpp
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <chrono>
using namespace std;

void erro(string s1 = "")
{
  cerr << s1 << endl;
  exit(1);
}

#define xdebug                                                                      \
  {                                                                                 \
    string st = "File=" + string(__FILE__) + " line=" + to_string(__LINE__) + "\n"; \
    cout << st;                                                                     \
  }

#define xprint(x)                \
  {                              \
    ostringstream os;            \
    os << #x " = " << x << '\n'; \
    cout << os.str();            \
  }

double timeSinceEpoch()
{
  // calculates the time elapsed since the epoch (typically January 1, 1970, for most systems)
  // in seconds as a floating-point number of type double.
  using namespace std::chrono;
  return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

typedef uint8_t BYTE;
typedef uint8_t GRY;

//<<<<<<<<<<<<<<< A partir daqui, deve linkar com OpenCV (aula 3) <<<<<<<<<<<<<<<<<<
#include <opencv2/opencv.hpp>
using namespace cv;
typedef Vec3b COR;

//<<<<<<<<<<<<<<< Funcoes de compatibilidade com Cekeikon <<<<<<<<<<<<<<<<<
inline void reta(Mat_<COR> &a, int li, int ci, int lf, int cf, COR cor = {0, 0, 0}, int grossura = 1)
{
  Scalar s(cor[0], cor[1], cor[2]);
  line(a, Point(ci, li), Point(cf, lf), s, grossura);
}

inline void flecha(Mat_<COR> &a, int li, int ci, int lf, int cf, COR cor = {0, 0, 0}, int grossura = 1)
{
  Scalar s(cor[0], cor[1], cor[2]);
  arrowedLine(a, Point(ci, li), Point(cf, lf), s, grossura);
}

inline void ponto(Mat_<COR> &b, int l, int c, COR cor = COR(0, 0, 0), int t = 1)
{
  if (t == 1 && 0 <= l && l < b.rows && 0 <= c && c < b.cols)
    b(l, c) = cor;
  else
  {
    int t2 = t / 2;
    int linic = max(l - t2, 0);
    int lfim = min(linic + t, b.rows - 1);
    int cinic = max(c - t2, 0);
    int cfim = min(cinic + t, b.cols - 1);
    for (int ll = linic; ll < lfim; ll++)
      for (int cc = cinic; cc < cfim; cc++)
        b(ll, cc) = cor;
  }
}

//<<<<<<<<<<<<<<<<<<< Definicoes da aula 4 <<<<<<<<<<<<<<<<<<<<<<<<<
typedef float FLT;
const double epsilon = FLT_EPSILON;

Mat_<FLT> matchTemplateSame(Mat_<FLT> a, Mat_<FLT> q, int method, FLT backg = 0.0)
{
  Mat_<FLT> p{a.size(), backg};
  Rect rect{(q.cols - 1) / 2, (q.rows - 1) / 2, a.cols - q.cols + 1, a.rows - q.rows + 1};
  Mat_<FLT> roi{p, rect};
  matchTemplate(a, q, roi, method);
  return p;
}

Mat_<FLT> somaAbsDois(Mat_<FLT> a) // Faz somatoria absoluta da imagem dar dois
{
  Mat_<FLT> d = a.clone();
  double soma = 0.0;
  for (auto di = d.begin(); di != d.end(); di++)
    soma += abs(*di);
  if (soma < epsilon)
    erro("Erro somatoriaUm: Divisao por zero");
  soma = soma / 2.0;
  for (auto di = d.begin(); di != d.end(); di++)
    (*di) /= soma;
  return d;
}

Mat_<FLT> dcReject(Mat_<FLT> a)
{ // Elimina nivel DC (subtrai media)
  a = a - mean(a)[0];
  return a;
}

Mat_<FLT> dcReject(Mat_<FLT> a, FLT dontcare)
{ // Elimina nivel DC (subtrai media) com dontcare
  Mat_<uchar> naodontcare = (a != dontcare);
  Scalar media = mean(a, naodontcare);
  subtract(a, media[0], a, naodontcare);
  Mat_<uchar> simdontcare = (a == dontcare);
  subtract(a, dontcare, a, simdontcare);
  return a;
}

void converte(Mat_<COR> ent, Mat_<FLT> &sai)
{
  Mat_<Vec3f> temp;
  ent.convertTo(temp, CV_32F, 1.0 / 255.0, 0.0);
  cvtColor(temp, sai, CV_BGR2GRAY);
}

//<<<<<<<<<<<<<<<<<<<<<< Definicoes da aula 5 <<<<<<<<<<<<<<<<<<<<<<<<<

template <class T>
void copia(Mat_<T> ent, Mat_<T> &sai, int li, int ci)
{
  // Copia ent para dentro de sai a partir de sai(li,ci).
  // sai deve vir alocado
  // ent deve ser normalmente menor que sai
  // Ha protecao para o caso de ent nao caber dentro de sai
  int lisai = max(0, li);
  int cisai = max(0, ci);
  int lfsai = min(sai.rows - 1, li + ent.rows - 1);
  int cfsai = min(sai.cols - 1, ci + ent.cols - 1);
  Rect rectsai = Rect(cisai, lisai, cfsai - cisai + 1, lfsai - lisai + 1);

  int lient = max(0, -li);
  int cient = max(0, -ci);
  int lfent = min(ent.rows - 1, sai.rows - 1 - li);
  int cfent = min(ent.cols - 1, sai.cols - 1 - ci);
  Rect rectent = Rect(cient, lient, cfent - cient + 1, lfent - lient + 1);

  if (rectsai.width > 0 && rectsai.height > 0 && rectent.width > 0 && rectent.height > 0)
  {
    Mat_<T> sairoi = sai(rectsai);
    Mat_<T> entroi = ent(rectent);
    entroi.copyTo(sairoi);
  }
}

//<<<<<<<<<<<< MNIST <<<<<<<<<<<<<<<<<<<<<<<
class MNIST
{
public:
  bool localizou;
  int nlado;
  bool inverte;
  bool ajustaBbox;
  string metodo;
  int na;
  vector<Mat_<GRY>> AX;
  vector<int> AY;
  Mat_<FLT> ax;
  Mat_<FLT> ay;
  int nq;
  vector<Mat_<GRY>> QX;
  vector<int> QY;
  Mat_<FLT> qx;
  Mat_<FLT> qy;
  Mat_<FLT> qp;

  MNIST(int _nlado = 28, bool _inverte = true, bool _ajustaBbox = true, string _metodo = "flann")
  {
    nlado = _nlado;
    inverte = _inverte;
    ajustaBbox = _ajustaBbox;
    metodo = _metodo;
  }
  Mat_<GRY> bbox(Mat_<GRY> a);                                         // Ajusta para bbox. Se nao consegue, faz localizou=false
  Mat_<FLT> bbox(Mat_<FLT> a);                                         // Ajusta para bbox. Se nao consegue, faz localizou=false
  void leX(string nomeArq, int n, vector<Mat_<GRY>> &X, Mat_<FLT> &x); // funcao interna
  void leY(string nomeArq, int n, vector<int> &Y, Mat_<FLT> &y);       // f. interna
  void le(string caminho = "", int _na = 60000, int _nq = 10000);
  // Le banco de dados MNIST que fica no path caminho
  // ex: mnist.le("."); mnist.le("c:/diretorio");
  // Se _na ou _nq for zero, nao le o respectivo
  // ex: mnist.le(".",60000,0);
  int contaErros();
  Mat_<GRY> geraSaida(Mat_<GRY> q, int qy, int qp); // f. interna
  Mat_<GRY> geraSaidaErros(int maxErr = 0);
  // Conta erros e gera imagem com maxErr primeiros erros
  Mat_<GRY> geraSaidaErros(int nl, int nc);
  // Gera uma imagem com os primeiros nl*nc digitos classificados erradamente
};

Mat_<GRY> MNIST::bbox(Mat_<GRY> a)
{
  // Ajusta para bbox. Se nao consegue, faz localizou=false
  int esq = a.cols, dir = 0, cima = a.rows, baixo = 0; // primeiro pixel diferente de 255.
  for (int l = 0; l < a.rows; l++)
    for (int c = 0; c < a.cols; c++)
    {
      if (a(l, c) != 255)
      {
        if (c < esq)
          esq = c;
        if (dir < c)
          dir = c;
        if (l < cima)
          cima = l;
        if (baixo < l)
          baixo = l;
      }
    }
  Mat_<GRY> d;
  if (!(esq < dir && cima < baixo))
  { // erro na localizacao
    localizou = false;
    d.create(nlado, nlado);
    d.setTo(128);
  }
  else
  {
    localizou = true;
    Mat_<GRY> roi(a, Rect(esq, cima, dir - esq + 1, baixo - cima + 1));
    resize(roi, d, Size(nlado, nlado), 0, 0, INTER_AREA);
  }
  return d;
}

Mat_<FLT> MNIST::bbox(Mat_<FLT> a)
{
  // Ajusta para bbox. Se nao consegue, faz localizou=false
  int esq = a.cols, dir = 0, cima = a.rows, baixo = 0; // primeiro pixel menor que 0.5.
  for (int l = 0; l < a.rows; l++)
    for (int c = 0; c < a.cols; c++)
    {
      if (a(l, c) <= 0.5)
      {
        if (c < esq)
          esq = c;
        if (dir < c)
          dir = c;
        if (l < cima)
          cima = l;
        if (baixo < l)
          baixo = l;
      }
    }
  Mat_<FLT> d;
  if (!(esq < dir && cima < baixo))
  { // erro na localizacao
    localizou = false;
    d.create(nlado, nlado);
    d.setTo(0.5);
  }
  else
  {
    localizou = true;
    Mat_<FLT> roi(a, Rect(esq, cima, dir - esq + 1, baixo - cima + 1)); // Consertei 5/11/2019
    resize(roi, d, Size(nlado, nlado), 0, 0, INTER_AREA);
  }
  return d;
}

void MNIST::leX(string nomeArq, int n, vector<Mat_<GRY>> &X, Mat_<FLT> &x)
{
  X.resize(n);
  for (unsigned i = 0; i < X.size(); i++)
    X[i].create(nlado, nlado);

  FILE *arq = fopen(nomeArq.c_str(), "rb");
  if (arq == NULL)
    erro("Erro: Arquivo inexistente " + nomeArq);
  BYTE b;
  Mat_<GRY> t(28, 28), d;
  fseek(arq, 16, SEEK_SET);
  for (unsigned i = 0; i < X.size(); i++)
  {
    if (fread(t.data, 28 * 28, 1, arq) != 1)
      erro("Erro leitura " + nomeArq);
    if (inverte)
      t = 255 - t;
    if (ajustaBbox)
    {
      d = bbox(t);
    }
    else
    {
      if (nlado != 28)
        resize(t, d, Size(nlado, nlado), 0, 0, INTER_AREA);
      else
        t.copyTo(d);
    }
    X[i] = d.clone();
  }
  fclose(arq);

  x.create(X.size(), X[0].total());
  for (int i = 0; i < x.rows; i++)
    for (int j = 0; j < x.cols; j++)
      x(i, j) = X[i](j) / 255.0;
}

void MNIST::leY(string nomeArq, int n, vector<int> &Y, Mat_<FLT> &y)
{
  Y.resize(n);
  y.create(n, 1);
  FILE *arq = fopen(nomeArq.c_str(), "rb");
  if (arq == NULL)
    erro("Erro: Arquivo inexistente " + nomeArq);
  BYTE b;
  fseek(arq, 8, SEEK_SET);
  for (unsigned i = 0; i < y.total(); i++)
  {
    if (fread(&b, 1, 1, arq) != 1)
      erro("Erro leitura " + nomeArq);
    Y[i] = b;
    y(i) = b;
  }
  fclose(arq);
}

void MNIST::le(string caminho, int _na, int _nq)
{
  na = _na;
  nq = _nq;
  if (na > 60000)
    erro("na>60000");
  if (nq > 10000)
    erro("nq>10000");

  if (na > 0)
  {
    leX(caminho + "/train-images.idx3-ubyte", na, AX, ax);
    leY(caminho + "/train-labels.idx1-ubyte", na, AY, ay);
  }
  if (nq > 0)
  {
    leX(caminho + "/t10k-images.idx3-ubyte", nq, QX, qx);
    leY(caminho + "/t10k-labels.idx1-ubyte", nq, QY, qy);
    qp.create(nq, 1);
  }
}

int MNIST::contaErros()
{
  // conta numero de erros
  int erros = 0;
  for (int l = 0; l < qp.rows; l++)
    if (qp(l) != qy(l))
      erros++;
  return erros;
}

Mat_<GRY> MNIST::geraSaida(Mat_<GRY> q, int qy, int qp)
{
  Mat_<GRY> d(28, 38, 192);
  // putTxt(d,0,28,to_string(qy));
  putText(d, to_string(qy), Point(28, 0), 0, 0.8, Scalar(0, 0, 0), 2);
  // putTxt(d,14,28,to_string(qp));
  putText(d, to_string(qp), Point(28, 14), 0, 0.8, Scalar(0, 0, 0), 2);
  int delta = (28 - q.rows) / 2;
  copia(q, d, delta, delta);
  return d;
}

Mat_<GRY> MNIST::geraSaidaErros(int maxErr)
{
  // Gera imagem 23x38, colocando qy e qp a direita.
  int erros = contaErros();
  Mat_<GRY> e(28, 40 * min(erros, maxErr), 192);
  for (int j = 0, i = 0; j < qp.rows; j++)
  {
    if (qp(j) != qy(j))
    {
      Mat_<GRY> t = geraSaida(QX[j], qy(j), qp(j));
      copia(t, e, 0, 40 * i);
      i++;
      if (i >= min(erros, maxErr))
        break;
    }
  }
  return e;
}

Mat_<GRY> MNIST::geraSaidaErros(int nl, int nc)
{
  // Gera uma imagem com os primeiros nl*nc digitos classificados erradamente
  Mat_<GRY> e(28 * nl, 40 * nc, 192);
  int j = 0;
  for (int l = 0; l < nl; l++)
    for (int c = 0; c < nc; c++)
    {
      // acha o proximo erro
      while (qp(j) == qy(j) && j < qp.rows)
        j++;
      if (j == qp.rows)
        goto saida;
      Mat_<GRY> t = geraSaida(QX[j], qy(j), qp(j));
      copia(t, e, 28 * l, 40 * c);
      j++;
    }
saida:
  return e;
}

//<<<<<<<<<<<<<<<<<<< MnistFlann <<<<<<<<<<<<<<<<<<<<<<<<<<<
class MnistFlann : public MNIST
{
public:
  using MNIST::MNIST;
  flann::Index *ind = 0;
  void train();
  FLT predictInterno(Mat_<FLT> query); // f. interna
  FLT predict(Mat_<FLT> query);
  void predict(); // Faz predicao de qx e armazena em qp
  void save(string nomeArq);
  void load(string nomeArq);
};

//<<<<<<<<<<<<<<<<<<< MnistFlann <<<<<<<<<<<<<<<<<<<<<<<<<<<
void MnistFlann::train()
{
  static flann::Index ind2(ax, flann::KDTreeIndexParams(4));
  ind = &ind2;
}

FLT MnistFlann::predictInterno(Mat_<FLT> query)
{
  // flann::Index ind(ax,flann::KDTreeIndexParams(4));
  vector<int> indices(1);
  vector<float> dists(1);
  ind->knnSearch(query, indices, dists, 1, flann::SearchParams(32));
  return ay(indices[0]);
}

FLT MnistFlann::predict(Mat_<FLT> query)
{
  Mat_<FLT> t = bbox(query);
  // xprint(t.isContinuous());
  // t.reshape(1,1); xprint(t.size()); // Nao funciona por algum motivo
  // return predictInterno(t);
  Mat_<FLT> t2(1, t.total());
  for (unsigned i = 0; i < t.total(); i++)
    t2(i) = t(i);
  return predictInterno(t2);
}

void MnistFlann::predict()
{
  qp.create(nq, 1);
  // #pragma omp parallel for
  for (int l = 0; l < qp.rows; l++)
  {
    qp(l) = predictInterno(qx.row(l));
  }
}

void MnistFlann::save(string nomeArq)
{
  ind->save(nomeArq);
}

void MnistFlann::load(string nomeArq)
{
  static flann::Index ind2(ax, flann::SavedIndexParams(nomeArq));
  ind = &ind2;
}

Mat_<FLT> normaliza(Mat_<FLT> a)
// Normaliza Mat_<FLT> para intevalo [0,1]
{
  FLT minimo, maximo;
  minimo = maximo = a(0, 0);
  for (int l = 0; l < a.rows; l++)
    for (int c = 0; c < a.cols; c++)
    {
      minimo = min(a(l, c), minimo);
      maximo = max(a(l, c), maximo);
    }
  double delta = maximo - minimo;
  if (delta < epsilon)
    return a;

  Mat_<FLT> d(a.rows, a.cols);
  for (int l = 0; l < a.rows; l++)
    for (int c = 0; c < a.cols; c++)
      d(l, c) = (a(l, c) - minimo) / delta;
  return d;
}

void converte(Mat_<FLT> ent, Mat_<COR> &sai)
{
  Mat_<GRY> temp;
  ent.convertTo(temp, CV_8U, 255.0, 0.0);
  cvtColor(temp, sai, CV_GRAY2BGR);
}
