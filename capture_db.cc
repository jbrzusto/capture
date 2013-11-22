/**
 * @file capture_db.cc
 *  
 * @brief  Manage a database for capture of raw radar samples.
 * 
 * @author John Brzustowski <jbrzusto is at fastmail dot fm>
 * @version 0.1
 * @date 2013
 * @license GPL v3 or later
 *
 */

#include "capture_db.h"
#include <iostream>
#include <stdexcept>
#include <iomanip>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

capture_db::capture_db (std::string filename, std::string sem_name, std::string shm_name) :
  pulses_per_transaction(512),
  radar_mode (-1),
  digitize_mode (-1),
  last_azi(1000), // larger than any real value, so first pulse always begins a new sweep
  sweep_count(0),
  st_record_pulse(0)
{
  sem_latest_pulse_timestamp = sem_open(sem_name.c_str(), O_CREAT | O_RDWR, S_IRWXU + S_IRWXG + S_IROTH);
  if (! sem_latest_pulse_timestamp) {
    throw std::runtime_error("Coudln't open semaphore");
  }

  shm_latest_pulse_timestamp = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, S_IRWXU + S_IRWXG + S_IROTH);
  if (! shm_latest_pulse_timestamp) {
    throw std::runtime_error("Coudln't open shared memory");
  };

  if (SQLITE_OK != sqlite3_open_v2(filename.c_str(),
                  & db,
                  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                   0))
    throw std::runtime_error("Couldn't open database for output");

  sqlite3_exec(db, "pragma journal_mode=WAL;", 0, 0, 0);
  sqlite3_exec(db, "pragma wal_autocheckpoint=5000;", 0, 0, 0);

  ensure_tables();

  set_retain_mode("full");

  ftruncate(shm_latest_pulse_timestamp, sizeof(double));
  latest_pulse_timestamp = (double *) mmap(0, sizeof(double), PROT_READ | PROT_WRITE, MAP_SHARED, shm_latest_pulse_timestamp, 0);
  *latest_pulse_timestamp = 0;
  sem_post (sem_latest_pulse_timestamp);
}

capture_db::~capture_db () {

  if (st_record_pulse) {
    sqlite3_exec(db, "commit;", 0, 0, 0);
    sqlite3_finalize(st_record_pulse);
    st_record_pulse = 0;
  };
  sqlite3_exec(db, "pragma journal_mode=delete;", 0, 0, 0);
  sqlite3_close (db);
  db = 0;
  sem_close (sem_latest_pulse_timestamp);
  munmap (latest_pulse_timestamp, sizeof(double));
  close (shm_latest_pulse_timestamp);
};

void
capture_db::ensure_tables() {
  sqlite3_exec(db,  R"(
     create table if not exists pulses (                                                               -- digitized pulses
     pulse_key integer not null primary key,                                                           -- unique ID for this pulse
     sweep_key integer not null,                                                                       -- groups together pulses from same sweep
     mode_key integer references modes (mode_key),                                                     -- additional pulse metadata describing sampling rate etc.
     ts double,                                                                                        -- timestamp for start of pulse
     trigs integer,                                                                                    -- trigger count, for detecting dropped pulses
     azi double,                                                                                       -- azimuth of pulse, relative to start of heading pulse (radians)
     elev double,                                                                                      -- elevation angle (radians)
     rot double,                                                                                       -- rotation of waveguide (polarization - radians)
     samples BLOB                                                                                      -- digitized samples for each pulse
   );
   create unique index if not exists pulses_ts on pulses (ts);                                         -- fast lookup of pulses by timestamp
   create index if not exists pulses_sweep on pulses (sweep_key);                                      -- fast lookup of pulses by sweep #

   create table if not exists geo (                                                                    -- geographic location of radar itself, over time
     ts double,                                                                                        -- timestamp for this geometry record
     lat double,                                                                                       -- latitude of radar (degrees N)
     lon double,                                                                                       -- longitude of radar (degrees E)
     alt double,                                                                                       -- altitude (m ASL)
     heading double                                                                                    -- heading pulse orientation (degrees clockwise from true north)
   );
   create unique index if not exists geo_ts on geo (ts);                                               -- fast lookup of geography by timestamp

   create table if not exists modes (                                                                  -- combined radar, digitizing, and retention modes
    mode_key integer not null primary key,                                                             -- unique ID for this combination of radar, digitizing, and retain modes
    radar_mode_key integer references radar_modes (radar_mode_key),                                    -- radar mode setting
    digitize_mode_key integer references digitize_modes (digitize_mode_key),                           -- digitizing mode setting
    retain_mode_key integer references retain_modes (retain_mode_key)                                  -- retain mode setting
  );

  create unique index if not exists i_modes on modes (radar_mode_key, digitize_mode_key, retain_mode_key); -- unique index on combination of modes

  create table if not exists radar_modes (                                                             -- radar modes
     radar_mode_key integer not null primary key,                                                      -- unique ID of radar mode
     power double,                                                                                     -- power of pulses (kW)
     plen double,                                                                                      -- pulse length (nanoseconds)
     prf double,                                                                                       -- nominal PRF (Hz)
     rpm double                                                                                        -- rotations per minute
   );

  create unique index if not exists i_radar_modes on radar_modes (power, plen, prf, rpm);              -- fast lookup of all range records in one retain mode

   create table if not exists digitize_modes (                                                         -- digitizing modes
     digitize_mode_key integer not null primary key,                                                   -- unique ID of digitizing mode
     rate double,                                                                                      -- rate of pulse sampling (MHz)
     format integer,                                                                                   -- sample format: (low 8 bits is bits per sample; high 8 bits is flags)
                                                                                                       -- e.g 8: 8-bit
                                                                                                       --    16: 16-bit
                                                                                                       --    12: 12-bits in lower end of 16-bits (0x0XYZ)
                                                                                                       -- flag: 256 = packed, in little-endian format
                                                                                                       --    e.g. 12 + 256: 12 bits packed:
                                                                                                       -- the nibble-packing order is as follows:
                                                                                                       --
                                                                                                       -- input:     byte0    byte1    byte2
                                                                                                       -- nibble:    A   B    C   D    E   F
                                                                                                       --            lo hi    lo hi    lo hi     
                                                                                                       --
                                                                                                       -- output:    short0           short1
                                                                                                       --            A   B   C   0    D   E   F   0
                                                                                                       --            lo         hi    lo         hi

     ns integer                                                                                        -- number of samples per pulse digitized
  );

  create table if not exists retain_modes (                                                            -- retention modes; specifies what portion of a sweep is retained; 
    retain_mode_key integer not null primary key,                                                      -- unique ID of retain mode
    name text not null                                                                                 -- label by which retain mode can be selected
  );

  insert or replace into retain_modes (retain_mode_key, name) values (1, 'full');                      -- ensure the 1st retain mode is always 'full'

  create table if not exists retain_mode_ranges (                                                      -- for each contiguous range of azimuth angles having the same rangewise pattern
    retain_mode_key integer references retain_modes (retain_mode_key),                                 -- which retain mode this range corresponds to
    azi_low double,                                                                                    -- low azimuth angle (degrees clockwise from North) closed end
    azi_high double,                                                                                   -- high azimuth (degrees clockwise from North) open end
    num_runs integer,                                                                                  -- number of runs in pattern; 0 means keep all samples
    runs BLOB                                                                                          -- 32-bit little-endian float vector of length 2 * numRuns, giving start[0],len[0],start[1],len[1],.
                                                                                                       --   all in metres
  );

  create index if not exists i_retain_mode on retain_mode_ranges (retain_mode_key);                    -- fast lookup of all range records in one retain mode
  create index if not exists i_retain_mode_azi_low on retain_mode_ranges (retain_mode_key, azi_low);   -- fast lookup of records by retain mode and azimuth low
  create index if not exists i_retain_mode_azi_high on retain_mode_ranges (retain_mode_key, azi_high); -- fast lookup of records by retain mode and azimuth high

  create table if not exists param_settings (                                                      -- timestamped parameter settings; e.g. radar or digitizer gain
    ts double,   -- real timestamp (GMT) at which setting became effective
    param text,  -- name of parameter
    val   double -- value parameter set to
 );

 create index if not exists i_param_setting_ts on param_settings (ts);
 create index if not exists i_param_setting_param on param_settings (param);
)", 0, 0, 0);
};

void 
capture_db::set_radar_mode (double power, double plen, double prf, double rpm) {
  sqlite3_stmt *st;
  sqlite3_prepare_v2(db, "insert or replace into radar_modes (power, plen, prf, rpm) values (?, ?, ?, ?)",
                     -1, & st, 0);
  sqlite3_bind_double (st, 1, power);
  sqlite3_bind_double (st, 2, plen);
  sqlite3_bind_double (st, 3, prf);
  sqlite3_bind_double (st, 4, rpm);
  sqlite3_step (st);
  radar_mode = sqlite3_last_insert_rowid (db);
  sqlite3_finalize (st);
  update_mode();
};

void 
capture_db::set_digitize_mode (double rate, int format, int ns) {
  sqlite3_stmt *st;
  sqlite3_prepare_v2(db, "insert or replace into digitize_modes (rate, format, ns) values (?, ?, ?)",
                     -1, & st, 0);
  sqlite3_bind_double (st, 1, rate);
  sqlite3_bind_int (st, 2, format);
  sqlite3_bind_int (st, 3, ns);
  sqlite3_step (st);
  digitize_mode = sqlite3_last_insert_rowid (db);
  digitize_rate = rate;
  digitize_format = format;
  digitize_ns = ns;
  if (format & FORMAT_PACKED_FLAG) {
    digitize_num_bytes = (ns * (format & 0xff) + 7) / 8;  // full packing, rounded up to nearest byte
  } else {
    digitize_num_bytes = ns * (((format & 0xff) + 7) / 8);  // each sample takes integer number of bytes
  }
  sqlite3_finalize (st);
  update_mode();
};  

void 
capture_db::record_geo (double ts, double lat, double lon, double elev, double heading) {
  sqlite3_stmt *st;
  sqlite3_prepare_v2(db, "insert into geo (ts, lat, lon, alt, heading) values (?, ?, ?, ?, ?)",
                     -1, & st, 0);

  sqlite3_bind_double (st, 1, ts);
  sqlite3_bind_double (st, 2, lat);
  sqlite3_bind_double (st, 3, lon);
  sqlite3_bind_double (st, 4, elev);
  sqlite3_bind_double (st, 5, elev);
  sqlite3_step (st);
  sqlite3_finalize (st);
};
  

void 
capture_db::record_pulse (double ts, int trigs, int n_ACPs, double azi, double elev, double rot, void * buffer) {
  if (! st_record_pulse) {
    sqlite3_prepare_v2(db, "insert into pulses (sweep_key, mode_key, ts, trigs, azi, elev, rot, samples) values (?, ?, ?, ?, ?, ?, ?, ?)",
                     -1, & st_record_pulse, 0);

    sqlite3_exec (db, "begin transaction", 0, 0, 0);
    pulses_written_this_trans = 0;
  }
  
  if (azi < last_azi) {
    ++sweep_count;
    // DEBUGGING:    std::cerr << "first pulse of new sweep: ts = " << std::setprecision(14) << ts << std::setprecision(3) << "; n_ACPs = " << n_ACPs << "; azi = " << azi << std::endl;
  }
  
  sqlite3_reset (st_record_pulse);
  sqlite3_bind_double (st_record_pulse, 1, sweep_count);
  sqlite3_bind_double (st_record_pulse, 2, mode);
  sqlite3_bind_double (st_record_pulse, 3, ts);
  sqlite3_bind_int    (st_record_pulse, 4, trigs);
  sqlite3_bind_double (st_record_pulse, 5, azi);
  sqlite3_bind_double (st_record_pulse, 6, elev);
  sqlite3_bind_double (st_record_pulse, 7, rot);
  if (is_full_retain_mode()) {
    sqlite3_bind_blob (st_record_pulse, 8, buffer, digitize_num_bytes, SQLITE_STATIC); 
  } else {
    // FIXME: figure out which bytes to copy
  }
  sqlite3_step (st_record_pulse);

  last_azi = azi;

  ++pulses_written_this_trans;
  if (pulses_written_this_trans == pulses_per_transaction) {
    
    sqlite3_exec (db, "commit", 0, 0, 0);
    sqlite3_finalize (st_record_pulse);
    st_record_pulse = 0;
    
    // store the timestamp of the latest committed pulse to shared memory, protected by a semaphore
    sem_wait (sem_latest_pulse_timestamp);
    * latest_pulse_timestamp = ts;
    sem_post (sem_latest_pulse_timestamp);
  }
};

void
capture_db::set_retain_mode (std::string mode)
{
  sqlite3_stmt * st;
  sqlite3_prepare_v2(db, "select retain_mode_key from retain_modes where name = ?", 
                     -1, & st, 0);

  sqlite3_bind_text (st, 1, mode.c_str(), -1, SQLITE_TRANSIENT);
  if (SQLITE_ROW != sqlite3_step (st)) {
    sqlite3_finalize (st);
    throw std::runtime_error(std::string("Non existent retain mode selected: '") + mode + "'");
  }
  retain_mode = sqlite3_column_int (st, 0);
  retain_mode_name = mode;
  sqlite3_finalize (st);
};

void 
capture_db::clear_retain_mode (std::string mode)
{
  // TODO
};

bool
capture_db::is_full_retain_mode () {
  return retain_mode == 1;
};

void
capture_db::update_mode() {
  // do nothing if a component mode is not set
  if (radar_mode <= 0 || digitize_mode <= 0 || retain_mode <= 0)
    return;
  sqlite3_stmt * st;
  sqlite3_prepare_v2(db, "insert or replace into modes (radar_mode_key, digitize_mode_key, retain_mode_key) values (?, ?, ?)",
                     -1, & st, 0);
  sqlite3_bind_int (st, 1, radar_mode);
  sqlite3_bind_int (st, 2, digitize_mode);
  sqlite3_bind_int (st, 3, retain_mode); 
  sqlite3_step (st);
  mode = sqlite3_last_insert_rowid (db);
  sqlite3_finalize (st);
};

void
capture_db::record_param (double ts, std::string param, double val) {
  sqlite3_stmt * st;
  sqlite3_prepare_v2(db, "insert into param_settings (ts, param, val) values (?, ?, ?)", 
                     -1, & st, 0);

  sqlite3_bind_double (st, 1, ts); 
  sqlite3_bind_text (st, 2, param.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_double (st, 3, val);
  sqlite3_step (st);
  sqlite3_finalize (st);
};

void
capture_db::set_pulses_per_transaction (int pulses_per_transaction) {
  this->pulses_per_transaction = pulses_per_transaction;
};

int
capture_db::get_pulses_per_transaction () {
  return pulses_per_transaction;
};


  
