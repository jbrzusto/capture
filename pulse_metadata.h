/*
  digdar.h - digitize radar pulses

  For each pulse, we return/print the following structure:


*/

#ifndef _PULSE_METADATA_H_
#define _PULSE_METADATA_H_

#include <stdint.h>

#define PULSE_METADATA_MAGIC 0xf00ff00fabcddcbaLL

typedef struct {
  uint64_t magic_number; // magic number for syncing headers; stores PULSE_METADATA_MAGIC
  uint64_t arp_clock;    // ADC clock count (125 MHz) at most recent ARP pulse
  uint32_t trig_clock;   // ADC clock count (125 MHz) at which trigger pulse occurred, relative to that at most recent ARP
  float    acp_clock;    // ACP clock in [0..1] representing fraction of full sweep since last ARP pulse
  uint32_t num_trig;     // number of trigger pulses seen since last ARP, regardless of the number digitized
  uint32_t num_arp;      // number of arp pulses since reset
  uint16_t data[1];      // stub; will hold all samples when allocated
}   __attribute__((packed))  pulse_metadata;


#endif /* _PULSE_METADATA_H_ */
