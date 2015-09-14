/* -*- c++ -*- */
/*
 * @file rpcapture.cc
 *  
 * @brief Capture raw radar pulses from stdin into a database.
 * Format of raw pulses is given in pulse_metadata.h
 * 
 * @author John Brzustowski <jbrzusto is at fastmail dot fm>
 * @version 0.1
 * @date 2014
 * @license GPL v3 or later
 *
 *
 * Adapated from:
 *
 *    gnuradio-3.0.0/usrp/host/apps/test_usrp_standard_rx.cc
 *
 * whose licence is
 *
 *    Copyright 2003,2006,2008,2009 Free Software Foundation, Inc.
 * 
 *    This file is part of GNU Radio
 *    
 *    GNU Radio is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 3, or (at your option)
 *    any later version.
 *    
 *    GNU Radio is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *    
 *    You should have received a copy of the GNU General Public License
 *    along with GNU Radio; see the file COPYING.  If not, write to
 *    the Free Software Foundation, Inc., 51 Franklin Street,
 *    Boston, MA 02110-1301, USA.
 *
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdio>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <getopt.h>
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <boost/program_options.hpp>
#include "capture_db.h"
#include "pulse_metadata.h"
#include "shared_ring_buffer.h"
#include "tcp_reader.h"

namespace po = boost::program_options;

#include <sched.h>

#define MAX_N_SAMPLES 16384
#define PULSES_PER_TRANSACTION 100

static void do_capture (capture_db * cap, unsigned short n_samples, unsigned n_pulses, const std::string & interface, const std::string & port);

double now() {
  static struct timespec ts;
  clock_gettime(CLOCK_REALTIME, & ts);
  return ts.tv_sec + ts.tv_nsec / 1.0e9;
};

static capture_db * cap = 0;

void die(int sig) {
  if (cap)
    delete cap;
};

int main(int argc, char *argv[])
{
  struct sigaction sa;
  sigset_t sigs;
  // sigfillset( &sigs);

  // sa.sa_handler = die;
  // sa.sa_mask = sigs;

  // sigaction (SIGINT, &sa, 0);
  // sigaction (SIGTERM, &sa, 0);
  // sigaction (SIGQUIT, &sa, 0);
  // sigaction (SIGSEGV, &sa, 0);
  // sigaction (SIGILL, &sa, 0);

  unsigned int		decim		   = 1;	// decimation rate

  unsigned short	n_samples	   = 3000;	// set the number of samples per pulse
  unsigned      	n_pulses	   = 6000;	// set the number of pulses to buffer from network

  std::string		filename	   = "capture_data.sqlite";
  std::string           port               = "12345";
  std::string           interface          = "0.0.0.0";
  int                   quiet              = false;     // don't output diagnostics to stdout
  po::options_description	cmdconfig("Usage: rpcapture [options] [filename]");

  cmdconfig.add_options()
    ("help,h", "produce help message")
    ("decim,d", po::value<unsigned int>(&decim), "set fgpa decimation rate (1, 2, 3, 4, 8, 1024, 8192, or 65536; default is 1)")
    ("n_samples,n", po::value<unsigned short>(&n_samples), "number of samples to collect per pulse; default is 512; max is 16384")
    ("n_pulses,p", po::value<unsigned>(&n_pulses), "number of pulses to buffer from digitizer on network; default is 6000")
    ("quiet,q", "don't output diagnostics")
    ("realtime,T", "try to request realtime priority for process")
    ("port,P", po::value<std::string>(&port), "listen for incoming data on tcp port PORT; default is 12345")
    ("interface,i", po::value<std::string>(&interface), "bind listen port on this interface; default is all interfaces (0.0.0.0)")
    ;

  po::options_description fileconfig("Input file options");
  fileconfig.add_options()
    ("filename", po::value<std::string>(), "output file")
    ;

  po::positional_options_description inputfile;
  inputfile.add("filename", -1);

  po::options_description config;
  config.add(cmdconfig).add(fileconfig);
  
  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).
	    options(config).positional(inputfile).run(), vm);
  po::notify(vm);
  
  if (vm.count("help")) {
    std::cout << cmdconfig << "\n";
    return 1;
  }

  if (vm.count("quiet")) 
    quiet = true;

  if (vm.count("interface"))
    interface = vm["interface"].as<std::string>();

  if (vm.count("port"))
    port = vm["port"].as<std::string>();

  if (vm.count("filename")) {
    filename = vm["filename"].as<std::string>();
  }

  if (vm.count("realtime")) {
    int policy = SCHED_RR;
    int pri = (sched_get_priority_max (policy) - sched_get_priority_min (policy)) / 2;
    int pid = 0;  // this process
    
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = pri;
    int result = sched_setscheduler(pid, policy, &param);
    if (result != 0){
      perror ("sched_setscheduler: failed to set real time priority");
    }
    else
      printf("SCHED_RR enabled with priority = %d\n", pri);
  }

  if (vm.count("n_samples"))
    n_samples = vm["n_samples"].as<unsigned short>();

  if (n_samples > MAX_N_SAMPLES)
    perror ("Too many samples requested; max is 16384");

  if (vm.count("n_pulses"))
    n_pulses = vm["n_pulses"].as<unsigned>();

  if (vm.count("decim"))
    decim = vm["decim"].as<unsigned int>();

  cap = new capture_db(filename);

  // assume short-pulse mode for Bridgemaster E

  cap->set_radar_mode( 25e3, // pulse power, watts
                        50, // pulse length, nanoseconds
                      1800, // pulse repetition frequency, Hz
                        28  // antenna rotation rate, RPM
                      );

  // record digitizing mode
  cap->set_digitize_mode( 125e6 / decim, // digitizing rate, Hz
                         16,   // only uses lowest 14 bits when decim == 1 or decim > 4
                          ((decim <= 4) ? decim : 1 ) * (1<<14 - 1), // scale: max sample value possible
                          n_samples  // samples per pulse
                         );

  cap->set_retain_mode ("full"); // keep all samples from all pulses

  double ts = now();
  cap->record_geo(ts, 
              45.371357, -64.402784, 30, // lat, lon, alt of FORCE VC radar site
              136.8); // heading offset, in degrees clockwise from north, for radar at FORCE VC

  try {
    do_capture (cap, n_samples, n_pulses, interface, port);
  } catch (std::runtime_error e)
    {
    };

  delete cap;
  return 0;
};

static void * 
run_reader(void * tcpr) {
  tcp_reader *ptcpr = (tcp_reader *) tcpr;
  ptcpr->go();
  return 0;
};

static void
do_capture  (capture_db * cap, unsigned short n_samples, unsigned n_pulses, const std::string &interface, const std::string &port)
{
#ifdef DEBUG
  int pulse_count = 0;
#endif

  uint16_t psize = sizeof(pulse_metadata) + sizeof(uint16_t) * (n_samples - 1);

  bool okay = true;
  
  bool got_arp = false;
  uint32_t num_arp = 0;
  uint32_t num_acp_at_arp = 0;

  shared_ring_buffer srb(psize, n_pulses);
  tcp_reader tcpr(interface, port, &srb);

  pthread_t read_thread;

  int pc = 0;
  if (pthread_create(& read_thread, NULL, & run_reader, & tcpr))
      throw std::runtime_error("Unable to create reader thread\n");

  for ( ;; ) {
    unsigned char * pulsebuf = srb.read_chunk();
    if (! pulsebuf) {
      // quit if tcp reader is done
      if (srb.is_done())
        break;
      usleep(10000); // sleep 10 ms before retrying
      continue;
    }
    pulse_metadata * meta = (pulse_metadata *) & pulsebuf[0];
    if (meta->magic_number == PULSE_METADATA_DONE_MAGIC)
      break;
    if (meta->magic_number != PULSE_METADATA_MAGIC) {
      std::cerr << "Bad Magic Number on radar pulse - quitting\n";
      break;
    }

    // realtime ts at start of pulse is ARP ts + 8 ns per ADC tick,
    // which is what meta->trig_clock provides

    double ts = meta->arp_clock_sec + 1.0e-9*(meta->arp_clock_nsec + 8 * meta->trig_clock);

    // calculate azimuth based on count of ACPs since most recent ARP.

    ++pc;
    cap->record_pulse (ts,
                       meta->num_trig,
                       meta->trig_clock,
                       meta->acp_clock,
                       meta->num_arp,
                       0, // constant 0 elevation angle for FORCE radar
                       0, // constant polarization for FORCE radar
                       (uint16_t *) & pulsebuf[sizeof(pulse_metadata) - sizeof(uint16_t)]);
#ifdef DEBUG
    if (++pulse_count == 500) {
      pulse_count = 0;
      int reader_index, writer_index, diff;
      srb.get_indices(reader_index, writer_index);
      diff = (writer_index - reader_index) % n_pulses;
      std::cerr << "Read index: " << reader_index << ";  Writer index: " << writer_index << "; npulses: " << n_pulses << "; diff: " << diff << std::endl;
    }
#endif
    srb.done_reading_chunk();
  }
}
