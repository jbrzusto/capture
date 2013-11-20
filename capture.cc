/* -*- c++ -*- */
/*
 * @file capture.cc
 *  
 * @brief Capture raw radar samples into a database
 * 
 * @author John Brzustowski <jbrzusto is at fastmail dot fm>
 * @version 0.1
 * @date 2013
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
#include <getopt.h>
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <usrp/usrp_bbprx.h>
#include <usrp/usrp_bytesex.h>
#include "fpga_regs_common.h"
#include "fpga_regs_bbprx.h"
#include <boost/program_options.hpp>
#include "capture_db.h"

namespace po = boost::program_options;

#include <sched.h>

#define MAX_N_SAMPLES 16384
#define PULSES_PER_TRANSACTION 100

static void do_capture (usrp_bbprx_sptr urx, capture_db * cap, int n_samples);

double now() {
  static struct timespec ts;
  clock_gettime(CLOCK_REALTIME, & ts);
  return ts.tv_sec + ts.tv_nsec / 1.0e9;
};

static capture_db * cap = 0;
static usrp_bbprx_sptr urx;

void die(int sig) {
  if (urx) {
    //    !urx->stop();
    //    !urx->set_active (false);
    delete urx.get();
  }
  if (cap)
    delete cap;
};

int main(int argc, char *argv[])
{
  struct sigaction sa;
  sigset_t sigs;
  sigfillset( &sigs);

  sa.sa_handler = die;
  sa.sa_mask = sigs;

  sigaction (SIGINT, &sa, 0);
  sigaction (SIGTERM, &sa, 0);
  sigaction (SIGQUIT, &sa, 0);
  sigaction (SIGSEGV, &sa, 0);
  sigaction (SIGILL, &sa, 0);

  int			which		   = 0;		// specify which USRP board
  unsigned int		decim		   = 16;	// decimation rate
  float			vid_gain	   = 0;		// video gain

  float			trig_gain	   = 0;		// trigger gain
  float			trig_thresh_excite = 50;	// trigger excitation threshold (50%)
  float			trig_thresh_relax  = 50;	// trigger relaxation threshold (50%)
  unsigned int	trig_latency	   = 0;		// min. clock ticks between consecutive trigger pulses
  unsigned short	trig_delay	   = 0;		// how many clock ticks to wait after trigger before digitizing signal

  float			hdg_gain	   = 0;		// heading gain
  float			hdg_thresh_excite = 50;	// heading excitation threshold (50%)
  float			hdg_thresh_relax  = 50;	// heading relaxation threshold (50%)
  unsigned int	hdg_latency	   = 64000000;		// min. clock ticks between consecutive heading pulses (default 1s)

  float			azi_gain	   = 0;		// azimuth gain
  float			azi_thresh_excite = 50;	// azimuth excitation threshold (50%)
  float			azi_thresh_relax  = 50;	// azimuth relaxation threshold (50%)
  unsigned int	azi_latency	   = 32000;		// min. clock ticks between consecutive azimuth pulses (default 0.5 ms)

  unsigned short	n_samples	   = 512;	// set the number of samples to collect per pulse
  int                   n_pulses	   = -1;	// set the number of pulses to collect; -1 means continuous
  bool                  counting	   = false;	// should USRP return fake data from a counter, rather than A/D input
  bool                  vid_negate	   = false;	// is video negated?
  bool                  raw_packets        = false;     // when collecting pulses, return raw usb packets, ignore dropped ones
  unsigned int          bbprx_mode         = 0;         // sampling mode
  unsigned int          signal_sources     = 0x00010203; // VID: RX_A_A, TRIG: RX_B_A, HDG: RX_A_B, AZI: RX_B_B

  std::string		filename	   = "capture_data.sqlite";
  int			fusb_block_size	   = 0;
  int			fusb_nblocks	   = 0;
  int                   quiet              = false;     // don't output diagnostics to stdout
  po::options_description	cmdconfig("Usage: capture [options] [filename]");

  cmdconfig.add_options()
    ("help,h", "produce help message")
    ("which,W", po::value<int>(&which), "select which USRP board")
    ("decim,d", po::value<unsigned int>(&decim), "set fgpa decimation rate (0-65535; default is 16)")
    ("video-gain,g", po::value<float>(&vid_gain), "set video gain in dB (0-20; default is 0)")

    ("trigger-gain,G", po::value<float>(&trig_gain), "set trigger gain in dB (0-20; default is 0)")
    ("trig-thresh-relax,r", po::value<float>(&trig_thresh_relax), "trigger relaxation threshold (% of max signal; default is 50%)")
    ("trig-thresh-excite,e", po::value<float>(&trig_thresh_excite), "trigger excitation threshold (% of max signal; default is 50%)")
    ("trig-delay,D", po::value<unsigned short>(&trig_delay), "clock ticks to wait after trigger before digitizing signal; default is 0")
    ("trig-latency,L", po::value<unsigned int>(&trig_latency), "min. clock ticks between consecutive triggers; default is 0")

    ("heading-gain", po::value<float>(&hdg_gain), "set heading gain in dB (0-20; default is 0)")
    ("heading-thresh-relax", po::value<float>(&hdg_thresh_relax), "heading relaxation threshold (% of max signal; default is 50%)")
    ("heading-thresh-excite", po::value<float>(&hdg_thresh_excite), "heading excitation threshold (% of max signal; default is 50%)")
    ("heading-latency", po::value<unsigned int>(&hdg_latency), "min. clock ticks between consecutive heading; default is 0")

    ("azimuth-gain", po::value<float>(&azi_gain), "set azimuth gain in dB (0-20; default is 0)")
    ("azimuth-thresh-relax", po::value<float>(&azi_thresh_relax), "azimuth relaxation threshold (% of max signal; default is 50%)")
    ("azimuth-thresh-excite", po::value<float>(&azi_thresh_excite), "azimuth excitation threshold (% of max signal; default is 50%)")
    ("azimuth-latency", po::value<unsigned int>(&azi_latency), "min. clock ticks between consecutive azimuth; default is 0")

    ("n_samples,n", po::value<unsigned short>(&n_samples), "number of samples to collect per pulse; default is 512; max is 16384")
    ("n_pulses,P", po::value<int>(&n_pulses), "number of pulses to collect; default is continuous")
    ("bbprx_mode,m", po::value<unsigned int>(&bbprx_mode), "sampling mode: 0 (default) = normal; 1 = raw video; 2 = raw trigger; 3 = raw ARP; 4 = raw ACP; 5 = raw ALL interleaved")
    ("signal_sources,s", po::value<unsigned int>(&signal_sources), "signal sources:  unsigned int 0xVVTTHHAA: each byte determines source for one signal (VV=video, TT=trigger, HH=heading, AA=azimuth) according to theses values: 0 = RX_A_A, 1=RX_B_A, 2=RX_A_B, 3=RX_B_B, 4=IO_RX_A_0, 5=IO_RX_A_1, 6=IO_RX_B_0, 7=IO_RX_B_1 (default: 0x00010203")
    ("fusb_block_size,F", po::value<int>(&fusb_block_size), "set fast usb block size")
    ("fusb_nblocks,N", po::value<int>(&fusb_nblocks), "set fast usb nblocks")
    ("vid_negate,v", "negate video signal (default is no)")
    ("raw_packets,R", "return data from raw USB packets; don't strip metadata; ignore dropped packets")
    ("counting,C", "obtain data from a counter instead of from A/D conversion (for debugging)")
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

  if (vm.count("counting"))
    counting = true;

  if (vm.count("quiet"))
    quiet = true;

  if (vm.count("vid_negate"))
    vid_negate = true;

  if(vm.count("filename")) {
    filename = vm["filename"].as<std::string>();
  }

  if(vm.count("raw_packets"))
    raw_packets = true;
  
  if(vm.count("realtime")) {
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

  if (n_samples > MAX_N_SAMPLES)
    perror ("Too many samples requested; max is 16384");

  if (bbprx_mode > BBPRX_MODE_MAX)
    perror ("BBPRX mode too high; max is 5");

  if (!quiet) {
    std::cout << "which:   " << which << std::endl;
    std::cout << "decim:   " << decim << std::endl;
    std::cout << "video gain:    " << vid_gain << std::endl;
    std::cout << "negate video: " << vid_negate << std::endl;

    std::cout << "trigger gain:    " << trig_gain << std::endl;
    std::cout << "trigger excite threshold:    " << trig_thresh_excite << std::endl;
    std::cout << "trigger relax threshold:    " << trig_thresh_relax << std::endl;
    std::cout << "trigger latency:    " << trig_latency << std::endl;
    std::cout << "trigger delay:    " << trig_delay << std::endl;

    std::cout << "heading gain:    " << hdg_gain << std::endl;
    std::cout << "heading excite threshold:    " << hdg_thresh_excite << std::endl;
    std::cout << "heading relax threshold:    " << hdg_thresh_relax << std::endl;
    std::cout << "heading latency:    " << hdg_latency << std::endl;

    std::cout << "azimuth gain:    " << azi_gain << std::endl;
    std::cout << "azimuth excite threshold:    " << azi_thresh_excite << std::endl;
    std::cout << "azimuth relax threshold:    " << azi_thresh_relax << std::endl;
    std::cout << "azimuth latency:    " << azi_latency << std::endl;

    std::cout << "samples: " << n_samples << std::endl;
    std::cout << "pulses: " << n_pulses << std::endl;
    std::cout << "counting?: " << counting << std::endl;
    std::cout << "sampling mode: " << bbprx_mode << std::endl;
    std::cout << "raw_packets?:" << raw_packets << std::endl;
  }      
  int mode = 0;

  if (counting)
    mode |= FPGA_MODE_COUNTING;

 urx =     usrp_bbprx::make (which, fusb_block_size, fusb_nblocks);

  if (urx == 0)
    perror ("usrp_bbprx::make");

  if (!urx->set_fpga_mode (mode))
    perror ("urx->set_fpga_mode");

  if (!urx->set_decim_rate ((unsigned int) (decim & 0xffff)))
    perror ("urx->set_decim_rate");

  if (!urx->set_chan_gain (signal_sources >> 24, vid_gain))
    perror ("urx->set_vid_gain");

  if (!urx->set_vid_negate (vid_negate))
    perror ("urx->set_vid_negate");

  if (!urx->set_chan_gain ((signal_sources >> 16) & 0xff, trig_gain))
    perror ("urx->set_trig_gain");

  if (!urx->set_trig_thresh_excite ((unsigned short) (4095.0 * trig_thresh_excite / 100.0)))
    perror ("urx->set_trig_thresh_excite");

  if (!urx->set_trig_thresh_relax ((unsigned short) (4095.0 * trig_thresh_relax / 100.0)))
    perror ("urx->set_trig_thresh_relax");

  if (!urx->set_trig_latency (trig_latency))
    perror ("urx->set_trig_latency");

  if (!urx->set_trig_delay (trig_delay))
    perror ("urx->set_trig_delay");

  if (!urx->set_chan_gain ((signal_sources >> 8) & 0xff, hdg_gain))
    perror ("urx->set_hdg_gain");

  if (!urx->set_ARP_thresh_excite ((unsigned short) (4095.0 * hdg_thresh_excite / 100.0)))
    perror ("urx->set_ARP_thresh_excite");

  if (!urx->set_ARP_thresh_relax ((unsigned short) (4095.0 * hdg_thresh_relax / 100.0)))
    perror ("urx->set_ARP_thresh_relax");

  if (!urx->set_ARP_latency (hdg_latency))
    perror ("urx->set_ARP_latency");


  if (!urx->set_chan_gain (signal_sources & 0xff, azi_gain))
    perror ("urx->set_ACP_gain");

  if (!urx->set_ACP_thresh_excite ((unsigned short) (4095.0 * azi_thresh_excite / 100.0)))
    perror ("urx->set_ACP_thresh_excite");

  if (!urx->set_ACP_thresh_relax ((unsigned short) (4095.0 * azi_thresh_relax / 100.0)))
    perror ("urx->set_ACP_thresh_relax");

  if (!urx->set_ACP_latency (azi_latency))
    perror ("urx->set_ACP_latency");


  if (!urx->set_n_samples (n_samples))
    perror ("urx->set_n_samples");

  if (!urx->set_bbprx_mode (bbprx_mode))
    perror ("urx->set_bbprx_mode");

  if (!urx->set_signal_sources (signal_sources))
    perror ("urx->set_signal_sources");

  if (!quiet) 
    std::cout << "block_size:" << urx->block_size() << std::endl;

  if (!urx->set_aux_digital_io ())
    perror ("urx->set_aux_digital_io");

  // start data xfers
  if (!urx->start())
    perror ("urx->start");

  if (!urx->set_active (true))
    perror ("urx->set_active");

  cap = new capture_db(filename, "capture_pulse_timestamp", "/capture_pulse_timestamp");

  // assume short-pulse mode for Bridgemaster E

  cap->set_radar_mode( 25e3, // pulse power, watts
                        50, // pulse length, nanoseconds
                      1800, // pulse repetition frequency, Hz
                        28  // antenna rotation rate, RPM
                      );

  // record digitizing mode
  cap->set_digitize_mode( 64e6 / (1 + decim), // digitizing rate, Hz
                         12,   // 12 bits per sample in 16-bit 
                         n_samples  // samples per pulse
                         );

  cap->set_retain_mode ("full"); // keep all samples from all pulses

  cap->set_pulses_per_transaction (PULSES_PER_TRANSACTION); // commit to keeping data for at least PULSES_PER_TRANSACTION pulses

  double ts = now();
  cap->record_geo(ts, 
              45.372657, -64.404823, 30, // lat, long, alt of Fundy Force radar site
              0); // heading offset, in degrees

  cap->record_param(ts, "vid_gain", vid_gain);
  cap->record_param(ts, "vid_negate", vid_negate);
  
  do_capture (urx, cap, n_samples);

  if (!urx->stop())
    perror ("urx->stop");

  if (!urx->set_active (false))
    perror ("urx->set_active");

  return 0;
}

static void
do_capture  (usrp_bbprx_sptr urx, capture_db * cap, int n_samples)
{
  unsigned short buf[5 * PULSES_PER_TRANSACTION][n_samples];

  pulse_metadata meta;

  unsigned int packets_dropped = 0;
  bool okay = true;

  for (int j = 0;/**/;/**/) {
      
    okay = urx->get_pulse(buf[j], false, &meta) && okay;

    double ts = now();
    // if (n_pulses < 0 && !okay) {
    //   fprintf(stderr, "n_read_errors=%d, n_overruns=%d, n_missing_USB_packets=%d, n_missing_data_packets=%d\n", 
    //           urx->n_read_errors, urx->n_overruns, urx->n_missing_USB_packets, urx->n_missing_data_packets);
    //   urx->n_read_errors = 0;
    //   urx->n_overruns = 0;
    //   urx->n_missing_USB_packets = 0;
    //   urx->n_missing_data_packets = 0;
    //   okay = true;
    // }

    cap->record_pulse (ts, // timestamp at PC; okay for now, use better value combining RTC, USRP clocks as usrp_pulse_buffer does
                       meta.n_trigs,
                       (meta.n_ACPs % 2048) * 360.0 / 2048.0,  // rough - based on 2048 ACPs per sweep
                       0, // constant 0 elevation angle for FORCE radar
                       0, // constant polarization for FORCE radar
                       & buf[j][0]);
    ++j;
    if (j == 5 * PULSES_PER_TRANSACTION)
      j = 0;
  }
}
