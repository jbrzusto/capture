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
#include <vector>
#include <string>
#include <sqlite3.h>

/**
   @class capture_db 
   @brief database of captured radar data
*/

struct radar_mode_t {
  double power;  //!< radar pulse power, in kilowatts
  double plen;   //!< radar pulse length, in nanoseconds
  double prf;    //!< radar pulse repetition frequency, in Hz
  double rpm;    //!< radar rotation rate, in revolutions per minute
};

struct digitize_mode_t {
  double rate; //!< digitization rate, in millions of samples per second
  int format; //!< sample format; bits per sample, possibly ORed with 0x200 to indicate "packed"
  int scale; //!< max sample value; takes into account summation 
  int ns; //!< samples per pulse
};

struct geo_info_t {
  double ts;  //!< timestamp of this fix
  double lat; //!< latitude (degrees N)
  double lon; //!< longitude (degrees E)
  double alt; //!< altitude (metres ASL)
  double heading; //!< heading, true (not magnetic) degrees clockwise from North of 1st pulse in sweep
};

class capture_db {
 public:

  //!< anonymous enum of sample formats
  enum {FORMAT_PACKED_FLAG = 512};

  //!< constructor which opens a connection to the SQLITE file
  capture_db (std::vector < std::string > file_pathlist, std::string file_prefix, int file_duration);

  //!< destructor which closes connection to the SQLITE file
  ~capture_db (); // close the database file

  
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
  
  //!< set geographic info
  void set_geo (double ts, double lat, double lon, double alt, double heading);

  //!< record data from a single pulse
  void record_pulse (double ts, uint32_t trigs, uint32_t trig_clock, float azi, uint32_t num_arp, float elev, float rot, void * buffer);

  //!< set a parameter
  void set_param (double ts, std::string param, double val);

  //!< set the number of pulses per transaction; caller is guaranteeing
  //!< data for this many consecutive pulses is effectively static
  //!< so that sqlite need not make a private copy before commiting the
  //!< INSERT transaction.
  void set_pulses_per_transaction(int pulses_per_transaction);

  //!< get the number of pulses per transaction
  int get_pulses_per_transaction();

 protected:
  int pulses_per_transaction; //!< number of pulses to write to database per transaction
  int pulses_written_this_trans; //!< number of pulses written to database for current transaction
  int mode_ID;        //!< combined unique ID of all modes (radar, digitize, retain)
  radar_mode_t radar_mode; //!< current radar mode
  int radar_mode_ID; //!< unique ID of current radar mode (negative means not set)
  digitize_mode_t digitize_mode; //!< current digitizer mode
  int digitize_mode_ID; //!< unique ID of current digitize mode (negative means not set)
  int retain_mode_ID; //!< unique ID of current retain mode (negative means retail all samples per pulse)
  std::string retain_mode_name; //!< name of retain mode
  geo_info_t geo_info; //!< current geo info

  std::vector < std::string > file_pathlist; //!< list of paths to try write files to
  std::string file_prefix; //!< first portion of database filename
  int file_duration; //!< approximate max file duration, in seconds

  double digitize_rate; //!< < current digitizer rate
  int digitize_format; //!< < current digitizer format
  int digitize_ns; //!< current digitizer number of samples per pulse
  int digitize_num_bytes; //!< current digitizer number of bytes per pulse

  uint32_t last_num_arp; //!< ARP count from previous pulse; a change here means we're on a new sweep
  long long int sweep_count; //!< sweep count

  time_
  sqlite3 * db; //<! handle to sqlite connection
  sqlite3_stmt * st_record_pulse; //!< pre-compiled statement for recording raw pulses

  int commits_per_checkpoint; //!< how many commits before we manually to a wal checkpoint
  int commit_count; //!< counter for commits to allow appropriate checkpointing

  
  //!< update overall mode, given a component mode (radar, digitize, retain) has changed
  void update_mode(); 

  //!< open a new database file, specifying first timestamp; throws if unable to open

  void open_db_file(boost::posix_time::ptime t); 

  //!< ensure required tables exist in database
  void ensure_tables ();

  //!< record the radar mode for subsequent capture
  void record_radar_mode ();

  //!< record the digitize mode
  void record_digitize_mode ();

  //!< record the retain mode
  void record_retain_mode ();

  //!< record geographic info
  void record_geo ();

  //!< record a parameter setting
  void record_param ();
};
