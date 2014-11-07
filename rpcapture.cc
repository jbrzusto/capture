/* -*- c++ -*- */
/*
 * @file capture.cc
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

namespace po = boost::program_options;

#include <sched.h>

#define MAX_N_SAMPLES 16384
#define PULSES_PER_TRANSACTION 100

static void do_capture (capture_db * cap, int n_samples);

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

  std::string		filename	   = "capture_data.sqlite";
  int                   quiet              = false;     // don't output diagnostics to stdout
  po::options_description	cmdconfig("Usage: rpcapture [options] [filename]");

  cmdconfig.add_options()
    ("help,h", "produce help message")
    ("decim,d", po::value<unsigned int>(&decim), "set fgpa decimation rate (1-65535; default is 1)")
    ("n_samples,n", po::value<unsigned short>(&n_samples), "number of samples to collect per pulse; default is 512; max is 16384")
    ("quiet,q", "don't output diagnostics")
    ("realtime,T", "try to request realtime priority for process")
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

  if (vm.count("filename")) {
    filename = vm["filename"].as<std::string>();
  }

  if (vm.count("realtime")) {
    int policy = SCHED_FIFO;
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
      printf("SCHED_FIFO enabled with priority = %d\n", pri);
  }

  if (vm.count("n_samples"))
    n_samples = vm["n_samples"].as<unsigned short>();

  if (n_samples > MAX_N_SAMPLES)
    perror ("Too many samples requested; max is 16384");

  if (vm.count("decim"))
    decim = vm["decim"].as<unsigned int>();

  cap = new capture_db(filename, "capture_pulse_timestamp", "/capture_pulse_timestamp");

  // assume short-pulse mode for Bridgemaster E

  cap->set_radar_mode( 25e3, // pulse power, watts
                        50, // pulse length, nanoseconds
                      1800, // pulse repetition frequency, Hz
                        28  // antenna rotation rate, RPM
                      );

  // record digitizing mode
  cap->set_digitize_mode( 125e6 / decim, // digitizing rate, Hz
                         14,   // 12 bits per sample in 16-bit 
                         n_samples  // samples per pulse
                         );

  cap->set_retain_mode ("full"); // keep all samples from all pulses

  cap->set_pulses_per_transaction (PULSES_PER_TRANSACTION); // commit to keeping data for at least PULSES_PER_TRANSACTION pulses

  double ts = now();
  cap->record_geo(ts, 
              45.371907, -64.402584, 30, // lat, lon, alt of Fundy Force radar site
              0); // heading offset, in degrees

  do_capture (cap, n_samples);

  return 0;
}

static void
do_capture  (capture_db * cap, int n_samples)
{
  uint16_t psize = sizeof(pulse_metadata) + sizeof(uint16_t) * (n_samples - 1);

  unsigned char pulsebuf [psize];
  pulse_metadata * meta = (pulse_metadata *) & pulsebuf[0];

  bool okay = true;
  
  bool got_arp = false;
  uint32_t num_arp = 0;
  uint32_t num_acp_at_arp = 0;

  for ( ;; ) {
    int n =  fread(& pulsebuf, psize, 1, stdin);
    if (1 != n) {
      fputs("Unable to read radar pulse - quitting\n", stderr);
      break;
    }

    if (meta->magic_number != PULSE_METADATA_MAGIC) {
      fputs("Bad Magic Number on radar pulse - quitting\n", stderr);
      break;
    }

    double ts = now();

    // calculate azimuth based on count of ACPs since most recent ARP.

    cap->record_pulse (ts, // timestamp at PC; okay for now, use better value combining RTC, digitizer clocks
                       meta->num_trig,
                       meta->acp_clock,
                       meta->num_arp,
                       0, // constant 0 elevation angle for FORCE radar
                       0, // constant polarization for FORCE radar
                       (uint16_t *) & pulsebuf[sizeof(pulse_metadata) - sizeof(uint16_t)]);
  }
}
