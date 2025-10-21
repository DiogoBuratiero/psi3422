// projeto.hpp - incluir nos programas do seu projeto
// Implementa SERVER/CLIENT com sendBytes/receiveBytes + testaBytes

#pragma once
#include "raspberry.hpp" // conforme a apostila orienta reutilizar utilitários/macro/typedefs
// Se seu raspberry.hpp ainda não define BYTE, descomente as 2 linhas a seguir:
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

using std::string;

bool testaBytes(BYTE *buf, BYTE b, int n)
{
  // Testa se n bytes da memoria buf possuem valor b
  bool igual = true;
  for (int i = 0; i < n; i++)
  {
    if (buf[i] != b)
    {
      igual = false;
      break;
    }
  }
  return igual;
}

// ====================== SERVER ======================
class SERVER
{
  const string PORT = "3490"; // porta usada no projeto (conforme apostila)
  const int BACKLOG = 1;      // fila de espera de conexões
  int sockfd = -1;            // socket para listen()
  int new_fd = -1;            // socket aceito por accept()
  struct addrinfo hints{}, *servinfo = nullptr, *p = nullptr;

  static void *get_in_addr(struct sockaddr *sa)
  {
    if (sa->sa_family == AF_INET)
      return &(((struct sockaddr_in *)sa)->sin_addr);
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
  }

public:
  SERVER()
  {
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // usa meu IP

    int rv = getaddrinfo(nullptr, PORT.c_str(), &hints, &servinfo);
    if (rv != 0)
      erro(string("getaddrinfo: ") + gai_strerror(rv));

    // percorre resultados e faz bind no primeiro possível
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

    std::puts("server: waiting for connections...");
    // conexão será aceita em waitConnection()
  }

  ~SERVER()
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
        continue; // tenta novamente
      char s[INET6_ADDRSTRLEN];
      inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
      std::printf("server: got connection from %s\n", s);
      // depois de aceitar, podemos fechar o socket de escuta se quisermos somente 1 cliente:
      close(sockfd);
      sockfd = -1;
      break;
    }
  }

  void sendBytes(int nBytesToSend, BYTE *buf)
  {
    if (new_fd == -1)
      erro("server: sendBytes sem conexão aceita");
    int total = 0;
    while (total < nBytesToSend)
    {
      int n = send(new_fd, buf + total, nBytesToSend - total, 0);
      if (n == -1)
        erro("server: erro em send");
      total += n;
    }
  }

  void receiveBytes(int nBytesToReceive, BYTE *buf)
  {
    if (new_fd == -1)
      erro("server: receiveBytes sem conexão aceita");
    int total = 0;
    while (total < nBytesToReceive)
    {
      int n = recv(new_fd, buf + total, nBytesToReceive - total, 0);
      if (n == -1)
        erro("server: erro em recv");
      if (n == 0)
        erro("server: conexão encerrada pelo cliente");
      total += n;
    }
  }

  void sendUint(uint32_t m)
  {
    uint32_t net = htonl(m); // host -> network (big-endian)
    sendBytes(4, (BYTE *)&net);
  }
  void receiveUint(uint32_t &m)
  {
    uint32_t net = 0;
    receiveBytes(4, (BYTE *)&net);
    m = ntohl(net); // network -> host
  }
};

// ====================== CLIENT ======================
class CLIENT
{
  const string PORT = "3490";
  int sockfd = -1;
  struct addrinfo hints{}, *servinfo = nullptr, *p = nullptr;

  static void *get_in_addr(struct sockaddr *sa)
  {
    if (sa->sa_family == AF_INET)
      return &(((struct sockaddr_in *)sa)->sin_addr);
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
  }

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
      break; // conectou
    }
    if (p == nullptr || sockfd == -1)
    {
      freeaddrinfo(servinfo);
      erro("client: failed to connect");
    }

    char s[INET6_ADDRSTRLEN];
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    std::printf("client: connecting to %s\n", s);
    freeaddrinfo(servinfo);
  }

  ~CLIENT()
  {
    if (sockfd != -1)
    {
      close(sockfd);
      sockfd = -1;
    }
  }

  void sendBytes(int nBytesToSend, BYTE *buf)
  {
    if (sockfd == -1)
      erro("client: sendBytes sem conexão");
    int total = 0;
    while (total < nBytesToSend)
    {
      int n = send(sockfd, buf + total, nBytesToSend - total, 0);
      if (n == -1)
        erro("client: erro em send");
      total += n;
    }
  }

  void receiveBytes(int nBytesToReceive, BYTE *buf)
  {
    if (sockfd == -1)
      erro("client: receiveBytes sem conexão");
    int total = 0;
    while (total < nBytesToReceive)
    {
      int n = recv(sockfd, buf + total, nBytesToReceive - total, 0);
      if (n == -1)
        erro("client: erro em recv");
      if (n == 0)
        erro("client: conexão encerrada pelo servidor");
      total += n;
    }
  }

  void sendUint(uint32_t m)
  {
    uint32_t net = htonl(m); // host -> network (big-endian)
    sendBytes(4, (BYTE *)&net);
  }
  void receiveUint(uint32_t &m)
  {
    uint32_t net = 0;
    receiveBytes(4, (BYTE *)&net);
    m = ntohl(net); // network -> host
  }
};
