/**
   @file tcp_reader.h
   @author John Brzustowski <jbrzusto is at fastmail dot fm>
   @version 0.1
   @date 2015
   @license GPL v2 or later
 */

#pragma once
#include <string>
#include "shared_ring_buffer.h"

/**
   @class tcp_reader
   @brief Read fixed-size chunks of data from a TCP socket and write them into a shared_ring_buffer.

*/

class tcp_reader {
 public:
  //! constructor
  tcp_reader (const std::string &interface, const std::string &port, shared_ring_buffer * buf);

  //! destructor
  ~tcp_reader ();

  //! bind socket, listen for connection, write incoming data to shared ring buffer
  void go();

 protected:
  //! interface on which to listen for a connection
  std::string interface;

  //! port on which to listen for a connection
  std::string port;

  //! pointer to shared ring buffer of chunks; we are the writer
  shared_ring_buffer * buf;
};
    
