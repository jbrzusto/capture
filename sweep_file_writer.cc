/**
 * @file sweep_file_writer.cpp
 *  
 * @brief  Write radar sweeps to a file
 * 
 * @author John Brzustowski <jbrzusto is at fastmail dot fm>
 * @version 0.1
 * @date 2015
 * @license GPL v2 or later
 *
 */

#include "sweep_file_writer.h"
#include <boost/filesystem.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

sweep_file_writer::sweep_file_writer (std::string folder, std::string site, std::string logfile, int max_pulses, int samples, 
                                      int fmt, double range0, double clock, int decim, std::string mode ) : 
  folder(folder),
  site(site),
  logfile(logfile),
  max_pulses(max_pulses),
  samples(samples),
  fmt(fmt),
  range0(range0),
  clock(clock),
  decim(decim),
  mode(mode)
{
  np = 0;
  nARP = -1;
  clock_buf = new uint32_t[max_pulses];
  trig_buf = new uint32_t[max_pulses];
  azi_buf = new float[max_pulses];
  sample_buf = new uint16_t[max_pulses * samples];
  logfs = new std::ofstream(logfile);
}



sweep_file_writer::~sweep_file_writer ()
{
  write_file();
  delete logfs;
  delete [] sample_buf;
  delete [] trig_buf;
  delete [] azi_buf;
  delete [] clock_buf;
};


int
sweep_file_writer::record_pulse (double ts, uint32_t trigs, uint32_t trig_clock, float azi, uint32_t num_arp, float elev, float rot, void * buffer)
{
  static double last_ts = -1;
  if (ts < last_ts) 
    std::cerr << "Time inversion: new pulse = " << ts << "; last pulse = " << last_ts << std::endl;
  last_ts = ts;
  if (nARP != num_arp) {
    if (nARP >= 0)
      write_file();
    nARP = num_arp;
  }

  if (np == max_pulses)
    return 1; // max pulse count exceeded

  if (np == 0)
    ts0 = ts;

  clock_buf[np] = trig_clock;
  azi_buf[np] = azi;
  trig_buf[np] = trigs;
  memcpy (& sample_buf[np * samples], buffer, ((fmt & 0xff) * samples + 7 ) / 8);
  ++np;
  return 0;
}

int 
sweep_file_writer::write_file() {
  
  time_t ts = (time_t) floor(ts0);
  int us = round(1000000 * fmod(ts0, 1.0));
  // path length:   FOLDER / YYYY-MM-DD / HH / SITE-YYYY-MM-DDTHH-MM-SS.UUUUUU.dat
  char path_buf[folder.length() + site.length() + 32 + 10];

  std::string tplate = folder;
  tplate += "/";

  tplate += site;
  tplate += "-%Y-%m-%dT%H-%M-%S";
  
  // get microseconds
  char us_buf[12];
  sprintf(us_buf, ".%06d.dat", us);
  tplate += us_buf;

  // create the filename
  int fnlen = tplate.length() + 6;
  char filename[fnlen + 1];
  strftime(filename, fnlen, tplate.c_str(), gmtime(& ts));
  
  boost::filesystem::path p(filename);

  FILE *f = fopen(p.string().c_str(), "wb");

  // put out two lines of text header
  fputs("DigDar radar sweep file\n", f);
  // fixme: add an arbitrary JSON property list using methods
  // addParam(std::string, double)
  // addParam(std::string, int)
  // addParam(std::string, std::string)

  fprintf(f, "{\"version\":\"%s\",\"arp\":%d,\"np\":%d,\"ns\":%d,\"fmt\":%d,\"ts0\":%.6f,\"tsn\":%.6f,\"range0\":%.3f,\"clock\":%.6f,\"decim\":%d,\"mode\":\"%s\",\"bytes\":%lu}\n",
          VERSION,
          nARP,
          np,
          samples,
          fmt,
          ts0,
          ts0 + (clock_buf[np - 1] - clock_buf[0]) / (1e6 * clock), // clock is in MHz
          range0,
          clock,
          decim,
          mode.c_str(),
          np * (sizeof(clock_buf[0]) + sizeof(azi_buf[0]) + sizeof(trig_buf[0]) + sizeof(sample_buf[0]) * samples)
          );

  // write each binary object
  fwrite(clock_buf, sizeof(clock_buf[0]), np, f);
  fwrite(azi_buf, sizeof(azi_buf[0]), np, f);
  fwrite(trig_buf, sizeof(trig_buf[0]), np, f);
  fwrite(sample_buf, sizeof(sample_buf[0]), np * samples, f);
  fclose(f);

  // report file written to logfile
  (*logfs) << p.string() << std::endl << std::flush;

  // mark buffers as empty
  np = 0;
  return 0;
};

const char * const sweep_file_writer::VERSION = "1.0.0";
