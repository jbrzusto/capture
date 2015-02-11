/**
   @file shared_ring_buffer.h
   @author John Brzustowski <jbrzusto is at fastmail dot fm>
   @version 0.1
   @date 2015
   @license GPL v2 or later
 */

#pragma once
#include <vector>
#include <pthread.h>
/**
   @class shared_ring_buffer
   @brief Buffer fixed-sized chunks of data between a reader and writer, 
   preserving chunk integrity.
*/

class shared_ring_buffer {
 public:
  //! constructor
  shared_ring_buffer (int chunk_size, int num_chunks);

  //! destructor
  ~shared_ring_buffer ();

  //! return a pointer to the next available chunk from the buffer, returning NULL if
  // no chunk is available;
  unsigned char * read_chunk ();

  //! get a pointer to the next available writable chunk in the buffer
  unsigned char * chunk_for_writing ();

  //! write data to the next available chunk in the buffer
  void write_chunk (unsigned char *p);

  //! let writer indicate they are using current chunk
  void begin_writing_chunk();

  //! let writer indicate they are done with current chunk
  void done_writing_chunk();

  //! is writer done with current chunk?
  bool is_done_writing_chunk();

  //! let reader indicate they are using current chunk
  void begin_reading_chunk();

  //! let reader indicate they are done with current chunk
  void done_reading_chunk();

  //! is reader done with current chunk?
  bool is_done_reading_chunk();

  //! let reader or writer indicate they are done
  void done();

  //! return true once reader or writer has disconnected
  bool is_done();

  //! return size of chunks, in bytes
  const int get_chunk_size ();

 protected:
  //! size of each chunk, in bytes
  int chunk_size;

  //! number of chunks allocated for ring buffer
  int num_chunks;

  //! buffer of chunks.
  std::vector < unsigned char > buf;

  //! chunk number currently being read; -1 means none
  int reader_chunk_index;

  //! chunk number currently being written to; -1 means none
  int writer_chunk_index;

  //! true iff chunk write for current index has completed
  bool chunk_write_complete;

  //! true iff chunk write for current index has completed
  bool chunk_read_complete;

  //! mutex to protect read/write above three fields indexes
  pthread_mutex_t index_mutex;

  //! flag set to true when either reader or writer is finished
  bool m_done;

};
    
