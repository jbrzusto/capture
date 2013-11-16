#include "capture_db.h"
#include <math.h>

main (int argc, char *argv[]) {
  
  capture_db cap("test_capture_db.sqlite");
  cap.set_radar_mode( 25e3, // pulse power, watts
                      100,  // pulse length, nanoseconds
                      1800, // pulse repetition frequency, Hz
                      28    // antenna rotation rate, RPM
                      );

  cap.set_digitize_mode( 64e6, // digitizing rate, Hz
                         12,   // 12 bits per sample in 16-bit 
                         1024  // samples per pulse
                         );

  cap.set_retain_mode( "full" ); // keep all samples from all pulses

  unsigned short dat[250][1024];

  dat[0][0] = 4095;
  dat[0][1] = 4095;
  dat[0][2] = 4094;

  struct timespec now;
  clock_gettime(CLOCK_REALTIME, & now);
    
  cap.record_geo(now.tv_sec + now.tv_nsec / 1.0e9, 45, -64, 20, 60);

  for (int j = 0; j < 250; ++j) {
    clock_gettime(CLOCK_REALTIME, & now);

    unsigned short *p = dat[j];
    if (j > 0) {
      unsigned short *q = dat[j-1];
      for (int i=0; i < 3; ++i)
        p[i] = 0.99 * .90674 * q[i] - 0.91234 * q[i+1] + 0.93462 * q[i+2];
    }
    for (int i = 3; i < 1024; ++i)
      p[i] = 0.99 * .90674 * p[i-3] - 0.91234 * p[i-2] + 0.93462 * p[i-1];
    
    cap.record_pulse(now.tv_sec + now.tv_nsec / 1.0e9, // timestamp now
                     fmod(j * 360.0 / 125, 360), // azimuth
                     5.0, // constant 5 degree elevation
                     0.0, // constant 0 degree rotation of waveguide
                     p    // buffer of pulses
                     );
  };
}
