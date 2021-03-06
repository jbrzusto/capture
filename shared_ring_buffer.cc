/**
   @file shared_ring_buffer.cc
   @author John Brzustowski <jbrzusto is at fastmail dot fm>
   @version 0.1
   @date 2015
   @license GPL v2 or later
 */

#include "shared_ring_buffer.h"
#include <stdexcept>
#include <memory.h>

shared_ring_buffer::shared_ring_buffer  (int chunk_size, int num_chunks) :
  chunk_size (chunk_size),
  num_chunks (num_chunks),
  buf (chunk_size * num_chunks),
  reader_chunk_index (-1),
  writer_chunk_index (-1),
  chunk_write_complete (false),
  chunk_read_complete (false),
  index_mutex (PTHREAD_MUTEX_INITIALIZER),
  m_done(false)
{
  if (chunk_size < 1)
    throw std::runtime_error("shared_ring_buffer: invalid chunk size; must be positive");

  if (num_chunks < 2)
    throw std::runtime_error("shared_ring_buffer: invalid number of chunks; must be >= 2");
};

shared_ring_buffer::~shared_ring_buffer ()
{
  pthread_mutex_destroy (& index_mutex);
};

unsigned char *
shared_ring_buffer::read_chunk() 
{
  int ci;
  pthread_mutex_lock(&index_mutex);

  // if we've caught up with the writer, fail; we have already read this chunk,
  // which the writer had finished, but the writer has not advanced to a new
  // chunk yet.  This fixes the problem of the reader passing the writer.
  if (reader_chunk_index == writer_chunk_index) {
    pthread_mutex_unlock(&index_mutex);
    return 0;
  }

  // try bump up to the next chunk in the ring
  ci = (1 + reader_chunk_index) % num_chunks;

  // if it's the writer's chunk, or if no chunks have been written,
  // we fail.  Note that we might fail because the writer
  // has passed us and ended up on the next chunk, which means
  // the writer is writing much faster than the reader, at least
  // sometimes, and a larger number of chunks should be used.
  if ((ci == writer_chunk_index && ! is_done_writing_chunk()) || writer_chunk_index == -1) {
    ci = -1;
  } else {
    reader_chunk_index = ci;
    begin_reading_chunk();
  };

  // Note: it's possible the writer has passed us, which means the
  // writer has been writing faster than the reader has been reading,
  // at least for a while.  In this case, we opt to skip forward,
  // favouring trying to keep up with the writer over always reading
  // the oldest chunk in the ring.  This results in a simpler
  // algorithm.  If this occurs (and the class provides no evidence
  // of it), the client code should use a larger number of chunks.

  pthread_mutex_unlock(&index_mutex);

  return ci >= 0 ? & buf[ci * chunk_size] : 0;
};

unsigned char * 
shared_ring_buffer::chunk_for_writing () {
  int ci;
  pthread_mutex_lock(&index_mutex);
  ci = (1 + writer_chunk_index) % num_chunks;
  if (ci == reader_chunk_index && ! is_done_reading_chunk())
    ci = (1 + ci) % num_chunks;
  writer_chunk_index = ci;
  begin_writing_chunk();
  pthread_mutex_unlock(&index_mutex);
  return & buf[ci * chunk_size];
};

void
shared_ring_buffer::write_chunk (unsigned char *p) {
  // Write data to the next available chunk.  Normally, it's the next
  // chunk in the ring, but we skip over the reader's current chunk.
  // Because there are at least two chunks in the ring, we are guaranteed
  // to find a destination chunk.

  memcpy(chunk_for_writing(), p, chunk_size);
  done_writing_chunk();
};

const int
shared_ring_buffer::get_chunk_size () {
  return chunk_size;
};

void
shared_ring_buffer::done() {
  m_done = true;
};

bool
shared_ring_buffer::is_done() {
  return m_done;
};

// the following are atomic reads/writes of a boolean
// value so we don't protect with the mutex

void
shared_ring_buffer::begin_writing_chunk() {
  chunk_write_complete = false;
};

void
shared_ring_buffer::done_writing_chunk() {
  chunk_write_complete = true;
};

bool
shared_ring_buffer::is_done_writing_chunk() {
  return chunk_write_complete;
};

void
shared_ring_buffer::begin_reading_chunk() {
  chunk_read_complete = false;
};

void
shared_ring_buffer::done_reading_chunk() {
  chunk_read_complete = true;
};

bool
shared_ring_buffer::is_done_reading_chunk() {
  return chunk_read_complete;
};

void
shared_ring_buffer::get_indices(int & reader_index, int & writer_index) {
  reader_index = reader_chunk_index;
  writer_index = writer_chunk_index;
  return;
};
