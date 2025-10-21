#pragma once
#include "raspberry.hpp" // precisa ter BYTE (uint8_t), erro(), etc.
// Se seu raspberry.hpp não define BYTE, descomente as 2 linhas abaixo:
// #include <cstdint>
// typedef uint8_t BYTE;

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <opencv2/opencv.hpp>
using cv::Mat_;
using cv::Vec3b;
typedef Vec3b COR; // 3 bytes BGR

using std::string;
using std::vector;

// ----------------- Utils de teste -----------------
inline bool testaBytes(BYTE *buf, BYTE b, int n)
{
  for (int i = 0; i < n; ++i)
    if (buf[i] != b)
      return false;
  return true;
}
inline bool testaVb(const vector<BYTE> &vb, BYTE b)
{
  for (BYTE x : vb)
    if (x != b)
      return false;
  return true;
}

// ==================================================
//                  CLASSE BASE (ABSTRATA)
// ==================================================
class DEVICE
{
public:
  // helper compatível IPv4/IPv6
  static void *get_in_addr(struct sockaddr *sa)
  {
    if (sa->sa_family == AF_INET)
      return &(((struct sockaddr_in *)sa)->sin_addr);
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
  }

  // Só as subclasses sabem "como" enviar/receber bytes
  virtual void sendBytes(int nBytesToSend, BYTE *buf) = 0;
  virtual void receiveBytes(int nBytesToReceive, BYTE *buf) = 0;

  // ---------- Métodos genéricos (definidos 1x só aqui) ----------
  // uint32_t em ordem de REDE (big-endian)
  void sendUint(uint32_t m)
  {
    uint32_t net = htonl(m);
    sendBytes(4, reinterpret_cast<BYTE *>(&net));
  }
  void receiveUint(uint32_t &m)
  {
    uint32_t net = 0;
    receiveBytes(4, reinterpret_cast<BYTE *>(&net));
    m = ntohl(net);
  }

  // vetor de bytes: envia tamanho (uint32) + dados
  void sendVb(const vector<BYTE> &vb)
  {
    sendUint(static_cast<uint32_t>(vb.size()));
    if (!vb.empty())
    {
      // const_cast é seguro aqui porque sendBytes não altera o conteúdo
      sendBytes((int)vb.size(), const_cast<BYTE *>(vb.data()));
    }
  }
  void receiveVb(vector<BYTE> &vb)
  {
    uint32_t n = 0;
    receiveUint(n);
    vb.assign(n, 0);
    if (n)
      receiveBytes((int)n, vb.data());
  }

  void sendImg(const Mat_<COR> &img)
  {
    if (!img.isContinuous())
      erro("sendImg: imagem nao-contigua (evite ROI)");
    uint32_t nl = (uint32_t)img.rows, nc = (uint32_t)img.cols;
    sendUint(nl);                            // rows
    sendUint(nc);                            // cols
    size_t nbytes = (size_t)3 * img.total(); // 3 canais (B,G,R)
    // Mat_ usa uchar* em data; convertemos para BYTE*
    sendBytes((int)nbytes, const_cast<BYTE *>(reinterpret_cast<const BYTE *>(img.data)));
  }

  // Recebe imagem e aloca buffer (contígua por padrão)
  void receiveImg(Mat_<COR> &img)
  {
    uint32_t nl = 0, nc = 0;
    receiveUint(nl);              // rows
    receiveUint(nc);              // cols
    img.create((int)nl, (int)nc); // Mat recém-criada é contígua
    size_t nbytes = (size_t)3 * img.total();
    receiveBytes((int)nbytes, reinterpret_cast<BYTE *>(img.data));
  }

  void sendImgComp(const Mat_<COR> &img)
  {
    if (!img.isContinuous())
      erro("sendImgComp: imagem nao-contigua (evite ROI)");
    // Compacta com qualidade 80 (exemplo da apostila)
    std::vector<uchar> vb;
    std::vector<int> params{cv::IMWRITE_JPEG_QUALITY, 80};
    if (!cv::imencode(".jpg", img, vb, params))
      erro("imencode falhou"); // :contentReference[oaicite:6]{index=6}
    // Protocolo: [len][bytes]
    uint32_t len = (uint32_t)vb.size();
    sendUint(len);
    if (len)
      sendBytes((int)len, reinterpret_cast<BYTE *>(vb.data()));
  }

  // Recebe imagem colorida COM compressão JPEG
  void receiveImgComp(Mat_<COR> &img)
  {
    uint32_t len = 0;
    receiveUint(len);
    std::vector<uchar> vb(len);
    if (len)
      receiveBytes((int)len, reinterpret_cast<BYTE *>(vb.data()));
    cv::Mat dec = cv::imdecode(vb, 1); // 1 = color  :contentReference[oaicite:7]{index=7}
    if (dec.empty())
      erro("imdecode retornou vazio");
    dec.copyTo(img); // garante Mat_<COR>
  }

  // (Se quiser depois, dá pra adicionar sendImg/receiveImg usando OpenCV)
  virtual ~DEVICE() = default;
};

// ==================================================
//                         SERVER
// ==================================================
class SERVER : public DEVICE
{
  const string PORT = "3490";
  const int BACKLOG = 1;
  int sockfd = -1; // listener
  int new_fd = -1; // conexão aceita
  struct addrinfo hints{}, *servinfo = nullptr, *p = nullptr;

public:
  SERVER()
  {
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rv = getaddrinfo(nullptr, PORT.c_str(), &hints, &servinfo);
    if (rv != 0)
      erro(string("getaddrinfo: ") + gai_strerror(rv));

    int yes = 1;
    for (p = servinfo; p != nullptr; p = p->ai_next)
    {
      sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
      if (sockfd == -1)
        continue;

      if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
      {
        close(sockfd);
        erro("setsockopt SO_REUSEADDR");
      }
      if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
      {
        close(sockfd);
        sockfd = -1;
        continue;
      }
      break; // bind OK
    }
    freeaddrinfo(servinfo);
    if (p == nullptr || sockfd == -1)
      erro("server: failed to bind");
    if (listen(sockfd, BACKLOG) == -1)
      erro("listen");

    std::puts("server: Esperando conexao...");
  }

  ~SERVER() override
  {
    if (new_fd != -1)
    {
      close(new_fd);
      new_fd = -1;
    }
    if (sockfd != -1)
    {
      close(sockfd);
      sockfd = -1;
    }
  }

  void waitConnection()
  {
    struct sockaddr_storage their_addr{};
    socklen_t sin_size = sizeof their_addr;

    while (true)
    {
      new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
      if (new_fd == -1)
        continue;
      char s[INET6_ADDRSTRLEN];
      inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
      std::printf("server: recebi conexao de %s\n", s);
      close(sockfd);
      sockfd = -1; // um cliente só
      break;
    }
  }

  // IMPLEMENTAÇÕES CONCRETAS (obrigatórias)
  void sendBytes(int nBytesToSend, BYTE *buf) override
  {
    if (new_fd == -1)
      erro("server: sendBytes sem conexao aceita");
    int total = 0;
    while (total < nBytesToSend)
    {
      int n = send(new_fd, buf + total, nBytesToSend - total, 0);
      if (n == -1)
        erro("server: erro em send");
      total += n;
    }
  }

  void receiveBytes(int nBytesToReceive, BYTE *buf) override
  {
    if (new_fd == -1)
      erro("server: receiveBytes sem conexao aceita");
    int total = 0;
    while (total < nBytesToReceive)
    {
      int n = recv(new_fd, buf + total, nBytesToReceive - total, 0);
      if (n == -1)
        erro("server: erro em recv");
      if (n == 0)
        erro("server: cliente fechou a conexao");
      total += n;
    }
  }
};

// ==================================================
//                         CLIENT
// ==================================================
class CLIENT : public DEVICE
{
  const string PORT = "3490";
  int sockfd = -1;
  struct addrinfo hints{}, *servinfo = nullptr, *p = nullptr;

public:
  explicit CLIENT(const string &endereco)
  {
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rv = getaddrinfo(endereco.c_str(), PORT.c_str(), &hints, &servinfo);
    if (rv != 0)
      erro(string("getaddrinfo: ") + gai_strerror(rv));

    for (p = servinfo; p != nullptr; p = p->ai_next)
    {
      sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
      if (sockfd == -1)
        continue;
      if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
      {
        close(sockfd);
        sockfd = -1;
        continue;
      }
      break;
    }
    if (p == nullptr || sockfd == -1)
    {
      freeaddrinfo(servinfo);
      erro("client: failed to connect");
    }

    char s[INET6_ADDRSTRLEN];
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    std::printf("client: conectando a %s\n", s);
    freeaddrinfo(servinfo);
  }

  ~CLIENT() override
  {
    if (sockfd != -1)
    {
      close(sockfd);
      sockfd = -1;
    }
  }

  // IMPLEMENTAÇÕES CONCRETAS (obrigatórias)
  void sendBytes(int nBytesToSend, BYTE *buf) override
  {
    if (sockfd == -1)
      erro("client: sendBytes sem conexao");
    int total = 0;
    while (total < nBytesToSend)
    {
      int n = send(sockfd, buf + total, nBytesToSend - total, 0);
      if (n == -1)
        erro("client: erro em send");
      total += n;
    }
  }

  void receiveBytes(int nBytesToReceive, BYTE *buf) override
  {
    if (sockfd == -1)
      erro("client: receiveBytes sem conexao");
    int total = 0;
    while (total < nBytesToReceive)
    {
      int n = recv(sockfd, buf + total, nBytesToReceive - total, 0);
      if (n == -1)
        erro("client: erro em recv");
      if (n == 0)
        erro("client: servidor fechou a conexao");
      total += n;
    }
  }
};
