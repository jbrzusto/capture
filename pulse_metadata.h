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
  uint32_t num_trig;   // number of trigger pulses counted so far, regardless of the number digitized
  uint64_t trig_clock;  // ADC clock count (125 MHz) at which trigger pulse occurred
  uint32_t num_acp;     // number of ACP seen
  uint64_t acp_clock;   // ADC clock count (125 MHz) at most recent ACP pulse
  uint32_t num_arp;     // number of ARP seen
  uint64_t arp_clock;   // ADC clock count (125 MHz) at most recent ARP pulse
  uint16_t data[1];    // really, will hold all samples when allocated
}   __attribute__((packed))  pulse_metadata;


#endif /* _PULSE_METADATA_H_ */
