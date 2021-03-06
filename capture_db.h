/**
 * @file capture_db.h
 *  
 * @brief Manage a database for capture of raw radar samples
 * 
 * @author John Brzustowski <jbrzusto is at fastmail dot fm>
 * @version 0.1
 * @date 2013
 * @license GPL v3 or later
 *
 */

#pragma once
#include <string>
#include <sqlite3.h>

/**
   @class capture_db 
   @brief database of captured radar data
*/

class capture_db {
 public:

  //!< anonymous enum of sample formats
  enum {FORMAT_PACKED_FLAG = 512};

  //!< constructor which opens a connection to the SQLITE file
  capture_db (std::string filename, int maxSweeps=0);

  //!< destructor which closes connection to the SQLITE file
  ~capture_db (); // close the database file

  //!< ensure required tables exist in database
  void ensure_tables ();
  
  //!< set the mode for subsequent capture
  void set_radar_mode (double power, double plen, double prf, double rpm);

  //!< set the digitize mode; returns the mode key
  void set_digitize_mode (double rate, int format, int scale, int ns);

  //!< set the retain mode
  void set_retain_mode (std::string mode);

  //!< clear the range records for a retain mode 
  void clear_retain_mode (std::string mode);

  //!< are we retaining all pulses per sample?
  bool is_full_retain_mode();
  
  //!< record geographic info
  void record_geo (double ts, double lat, double lon, double alt, double heading);

  //!< record data from a single pulse
  void record_pulse (double ts, uint32_t trigs, uint32_t trig_clock, float azi, uint32_t num_arp, float elev, float rot, void * buffer);

  //!< record a parameter setting
  void record_param (double ts, std::string param, double val);

 protected:
  int max_sweeps;   //!< if positive, maximum number of sweeps to store
                   //! in this database; when full, incoming sweeps
                   //! replace the oldest existing sweep.
  int mode;        //!< combined unique ID of all modes (radar, digitize, retain)
  int radar_mode; //!< unique ID of current radar mode (negative means not set)
  int digitize_mode; //!< unique ID of current digitize mode (negative means not set)
  int retain_mode; //!< unique ID of current retain mode (negative means retail all samples per pulse)
  std::string retain_mode_name; //!< name of retain mode

  double digitize_rate; //!< < current digitizer rate
  int digitize_format; //!< < current digitizer format
  int digitize_ns; //!< current digitizer number of samples per pulse
  int digitize_num_bytes; //!< current digitizer number of bytes per pulse

  uint32_t last_num_arp; //!< ARP count from previous pulse; a change here means we're on a new sweep
  long long int sweep_count; //!< sweep count
  long long int sweeps_in_db; //!< sweep count (in database; can go down when a sweep is deleted for case max_sweeps > 0)

  sqlite3 * db; //<! handle to sqlite connection
  sqlite3_stmt * st_record_pulse; //!< pre-compiled statement for recording raw pulses
  sqlite3_stmt * st_delete_oldest_sweep; //!< pre-compiled statement for deleting oldest sweep

  int commits_per_checkpoint; //!< how many commits before we manually do a wal checkpoint
  int commit_count; //!< counter for commits to allow appropriate checkpointing

  //!< update overall mode, given a component mode (radar, digitize, retain) has changed
  void update_mode(); 
};
