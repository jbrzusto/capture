/**
 * @file scan_converter.h
 *  
 * @brief Manage conversion of polar radar data to rectangular coordinates
 * 
 * @author John Brzustowski <jbrzusto is at fastmail dot fm>
 * @version 0.1
 * @date 2013
 * @license GPL v2 or later
 *
 */

#pragma once

#define DO_SCAN_CONVERSION_SMOOTHING
// keep undefined: #define DO_ALPHA_BLENDING

#define SCVT_EXTRA_PRECISION_BITS 4
#define SCVT_EXTRA_PRECISION_FACTOR (1 << SCVT_EXTRA_PRECISION_BITS)
#define SCVT_EXTRA_PRECISION_DROP (SCVT_EXTRA_PRECISION_FACTOR - 1)

#define SCVT_NODATA_VALUE 0x80000001
#define SCVT_IND(x)      inds[num_inds++] =   (x)
// negative cache entries mark the last index for a given pixel; we use "~" instead of "-" 
// so that index zero can still be marked as last.
#define SCVT_IND_LAST(x) inds[num_inds++] = (~(x))
#define SCVT_NO_IND      inds[num_inds++] = SCVT_NODATA_VALUE

// the number of bits of fractional precision to apply in zooming existing
// indexes; this is the number of fractional bits used in representing old->pps / pps

#define SCVT_ZOOM_FACTOR_PRECISION_BITS 16     

/**
   @class scan_converter 
   @brief conversion of polar radar data to rectangular coordinates
   Structure managing the sparse linear transformations we use for filling a
   cartesian buffer with data from the appropriate part of a polar buffer.  The
   polar buffer has na * nr slots in angle-major order.  The cartesian buffer
   has w * h slots in row-major order.  We maintain sufficient information
   about the transformation to tell whether its coefficients need to be 
   regenerated.

*/

#include <stdint.h>

typedef uint16_t      t_sample;
typedef uint32_t      t_pixel;
typedef uint32_t      t_palette;

class scan_converter {

 public:
  int nr, nc;  // dimensions of source data buffer in angle count, radius count
               // data must be stored in increasing radius within increasing angle
               // FIXME:  for now we assume the angles are evenly spaced

  int w, h;  // dimensions of image sub-buffer in pixels (width, height)

  int x0, y0;  // offset in pixels from left/top of image buffer to left/top of sub-buffer

  int xc, yc;  // offset in pixels from left/top of image buffer to centrepoint of source data

  bool always_smooth_angular; // do we always use multi-pulse smoothing, regardless of range?

  double scale; // number of output slots per input slot along the cartesian axes

  double first_range; // range of first sample

  double azi_begin;// azimuth [0..1] of first pulse
  double azi_end;  // azimuth [0..1] of last pulse

  double azi_step; // azimuth step [0..1] of input pulses (will be constant)

  int first_row_offset;    // index of the first row to be used in the source data
                           // FIXME: we wrap this offset around on the assumption the source data
                           // form a complete circle

  scan_converter ( int nr,
		   int nc,
		   int w,
		   int h,
		   int x0,
		   int y0,
		   int xc,
		   int yc,
                   bool always_smooth_angular,
		   double scale,
		   double first_range,
                   double azi_begin,
                   double azi_end
                   );

  ~scan_converter();

  void apply (t_sample *samp, 
              t_pixel *pix,
              int span,
              t_palette *pal,
              int sample_origin,
              int sample_scale
              );
  
 protected:
  // We don't use floating point coefficients.  Instead, for each output slot,
  // we maintain a list of up to 5 indexes of slots in the input buffer.  The
  // value for the output slot is then obtained as the average of these input
  // slots.

  int inds_alloc; // how big have we allocated the index list
  int num_inds; // how many indices are actually in index list
  int *inds; // pointer to the indexes; if NULL, there are none
};
  


