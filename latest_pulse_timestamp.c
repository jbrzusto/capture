/*
 * @file latest_pulse_timestamp.c
 *  
 * @brief Read the latest timestamp of a commited pulse in the current capture database.
 * 
 * @author John Brzustowski <jbrzusto is at fastmail dot fm>
 * @version 0.1
 * @date 2013
 * @license GPL v3 or later
 *
 */

#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

sem_t *sem_latest_pulse_timestamp = 0;
int shm_latest_pulse_timestamp = 0;
double *ptr_latest_pulse_timestamp = 0;

double latest_pulse_timestamp () {
  double ts;
  if (! sem_latest_pulse_timestamp) {
    sem_latest_pulse_timestamp = sem_open("capture_pulse_timestamp", O_RDWR);
    if (! sem_latest_pulse_timestamp)
      return 0;
  }

  if (! shm_latest_pulse_timestamp) {
    shm_latest_pulse_timestamp = shm_open("/capture_pulse_timestamp", O_CREAT | O_RDWR, S_IRWXU + S_IRWXG + S_IROTH);
    if (! shm_latest_pulse_timestamp) {
      return 0;
    };
    ftruncate(shm_latest_pulse_timestamp, sizeof(double));
    ptr_latest_pulse_timestamp = (double *) mmap(0, sizeof(double), PROT_READ | PROT_WRITE, MAP_SHARED, shm_latest_pulse_timestamp, 0);
  }

  sem_wait (sem_latest_pulse_timestamp);
  ts = * ptr_latest_pulse_timestamp;
  sem_post (sem_latest_pulse_timestamp);
  return ts;
};
