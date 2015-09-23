/**
   @file tcp_reader.cc
   @author John Brzustowski <jbrzusto is at fastmail dot fm>
   @version 0.1
   @date 2015
   @license GPL v2 or later
 */

#include "tcp_reader.h"

#ifdef DEBUG
#include "pulse_metadata.h"
#endif

#include <stdexcept>

#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <iostream>

tcp_reader::tcp_reader (const std::string &interface, const std::string &port, shared_ring_buffer * buf) :
  interface(interface),
  port(port),
  buf(buf)
{
};

tcp_reader::~tcp_reader () {
};

void
tcp_reader::go() {
  struct addrinfo hints;
  struct addrinfo *result, *local;
  struct sockaddr_storage peer_addr;
  socklen_t peer_addr_len;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;        /* Allow IPv4 only */
  hints.ai_socktype = SOCK_STREAM;  /* Stream socket */
  hints.ai_flags = 0;
  hints.ai_protocol = 0;            /* Any protocol */

  int s = getaddrinfo(interface.c_str(), port.c_str(), &hints, &result);
  if (s != 0) {
    std::string err("getaddrinfo: ");
    err += gai_strerror(s);
    err += '\n';
    throw std::runtime_error (err);
  }
  
  int infd;

  for (local = result; local != NULL; local = local->ai_next) {
    infd = socket(local->ai_family, local->ai_socktype,
                  local->ai_protocol);
    if (infd == -1)
      continue;

    int enable = 1;
    setsockopt(infd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    
    if (bind(infd, local->ai_addr, local->ai_addrlen) != -1)
      break;                  /* Success */
    
    close(infd);
  }
  
  if (local == NULL) {               /* No address succeeded */
    throw std::runtime_error("Could not bind to listening address and port\n");
  }

  listen(infd, 0);

  socklen_t peer_len = sizeof(peer_addr);
  memset( (char *) &peer_addr, 0, peer_len);
  
  // return code from read;
  int m;

  // listen for one connection
  int fd = accept(infd, local->ai_addr, &local->ai_addrlen);
    
  // pulse count
  int pc = 0;

#ifdef DEBUG
  double last_ts = -1;
#endif

  // read from the connection for as long as there are data
  do {
    // get the location of an available chunk 
    unsigned char * p = buf->chunk_for_writing();
#ifdef DEBUG
    pulse_metadata *p0 = (pulse_metadata *) p;
#endif
    int n = buf->get_chunk_size();
    // read into the current chunk until full
    int m;
    do {
      m = read(fd, p, n);
      if (m < 0 || errno != 0)
        break;
      if (m == 0) {
        pthread_yield();
        usleep(1000);
        continue;
      }
      p += m;
      n -= m;
    } while (n > 0);
    buf->done_writing_chunk();
#ifdef DEBUG
    double ts = p0->arp_clock_sec * 1.0 + p0->arp_clock_nsec * 1.0e-9;
    if (ts < last_ts)
      std::cerr << "tcpreader: time inversion from " << last_ts << " to " << ts << std::endl;
    last_ts = ts;

#endif
    ++pc;
    //    std::cerr << "Read pulses from socket: " << pc << "\n";
  } while (m >= 0);
  close(fd);
  buf->done();
};
