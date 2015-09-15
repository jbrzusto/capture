/**
 * @file sweep_file_writer.h
 *  
 * @brief write sweeps to files
 * 
 * @author John Brzustowski <jbrzusto is at fastmail dot fm>
 * @version 0.1
 * @date 2015
 * @license GPL v2 or later
 *
 */

#pragma once
#include <string>
#include <sqlite3.h>

#include <time.h>
#include <stdint.h>

/**
   @class sweep_file_writer 
   @brief accumulate pulses into a sweep and write them to a file
   Sweeps are written into a top-level folder, with hierarchy like:
      <TOPLEVEL>/YYYY-MM-DD / HH / SITE-YYYYMMDDTHHMMSS.MMMMMM_sweep.dat
   The timestamp is that of the first pulse in the sweep.
   Each .dat file consists of two '\n'-terminated ascii lines, then binary data.
   The first line is 'digdar sweepfile version A.B.C\n'
   The second line is a JSON formatted structure like this (newlines shown here
   are not part of the string):
      {
        "np": PULSES_IN_FILE,              // integer: pulses in file
        "ns": SAMPLES,           // integer: samples per pulse
        "fmt": FORMAT,                     // integer: bits per sample with or'd flags 
        "ts0": TS_PULSE_0,                 // double: timestamp of first pulse
        "tsn": TS_PULSE_n,                 // double: timestamp of last pulse
        "range0": RANGE_SAMPLE_0,          // double: range of first sample, in metres
        "clock": DIGITIZING_CLOCK_RATE,    // double: rate of digitizing clock, in MHz
        "decim": DECIMATION_RATE,          // int: number of clock samples per file sample
        "mode": "DECIMATION_MODE"          // string: "first", "mean", "sum"; how clock samples are converted to file sample
      }\n
   Then follows blocks of items, each block having one item per pulse.
   clocks:  np x 32-bit int; number of digitizing clocks since ARP for this pulse
   azi: np x 32-bit float; fraction of sweep 0...1 for this pulse
   trigs: np x 32-bit int; number of trigger pulses since ARP, including missed pulses
   samples: np x ns 16-bit int; samples, stored in increasing time order (closest to farthest within a pulse,
            earliest pulse to latest pulse)

   All are little-endian.

   For expansion, extra content can be added to the JSON string, and extra columns can be appended to
   the binary portion.
*/

class sweep_file_writer {
 public:

  //!< constructor
  sweep_file_writer (std::string folder, std::string site, int max_pulses, int samples, 
                     int fmt, double range0, double clock, int decim, std::string mode );

  //!< destructor; save accumulated sweep to file
  ~sweep_file_writer (); // close the database file

  //!< record data from a single pulse; if a new sweep is detected, save the existing sweep to the appropriate
  // file, and clear buffers before recording this pulse. Returns 0 on success.

  int record_pulse (double ts, uint32_t trigs, uint32_t trig_clock, float azi, uint32_t num_arp, float elev, float rot, void * buffer);

 protected:

  std::string folder; //!< path to top-level folder
  std::string site;   //!< name of site
  int max_pulses; //!< max number of pulses in a sweep
  int samples; //!< samples per pulse
  int fmt; //!< sample format: lowest 8 bits is bits per sample; higher bits are flags (none so far)
  double range0; //!< range of first sample in each pulse, in metres
  double clock; //!< sampling clock rate, in MHz
  int decim; //!< clock samples per file sample
  std::string mode; //!< how clock samples are converted to files sample; eg. "first", "sum", "mean"

  int np; //!< number of pulses in this sweep so far.
  int nARP; //!< ARP count of currently accumulating sweep; -1 means no sweep so far
  double ts0; //!< timestamp at first pulse
  uint32_t * clock_buf; //!< buffer of clocks for each pulse
  float    * azi_buf;    //!< buffer of azimuth values for each pulse
  uint32_t * trig_buf;  //!< buffer of trigger pulse counts for each pulse
  uint16_t * sample_buf; //!< buffer of all samples for all pulses

  int write_file(); //!< write accumulated pulses to appropriate file, and clear buffers, returning 0 on success

};
