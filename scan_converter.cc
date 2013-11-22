#include "scan_converter.h"
#include <cmath>

scan_converter::scan_converter ( int nr,
                                 int nc,
                                 int w,
                                 int h,
                                 int x0,
                                 int y0,
                                 int xc,
                                 int yc,
                                 bool always_smooth_angular,
                                 double scale,
                                 double first_angle,
                                 double first_range
                                 ) :
  nr(nr),
  nc(nc),
  w(w),
  h(h),
  x0(x0),
  y0(y0),
  xc(xc),
  yc(yc),
  scale(scale),
  first_angle(first_angle),
  first_range(first_range),
  always_smooth_angular(always_smooth_angular),
  inds(0)
{
  // create a scan converter for mapping polar to cartesian data
  // 
  // nr, nc: dimensions of the polar data:  nr angular rows of nc radial slots each
  // w, h: dimensions of output (sub) block
  // x0, y0: offset of output (sub) block in output buffer
  // xc, yc: offset of polar centre in output buffer (need not 
  //         be within the output sub block)
  // always_smooth_angular: if true, always do smoothing across pulses
  // scale:  pixels per sample 
  // first_angle:  angle (in radians clockwise from the output horizontal axis) of the first row of source data
  // first_range:  range of first sample, measured in range-cell size.  This need not be an integer.
  //               Negative means there are bogus (pre-trigger)
  //               samples at the start of each pulse; positive means there are missing samples.

  char use_radial_neighbours;	/* true if radially neighbouring input slots are used for each output slot */
  int angular_neighbour_thresh; /* the minimum (in pixels) at which angular neighbours are used for each pixel */
  int i, j, l;
  int ihi, jhi;
  int range, theta;
  double x, y;
  double theta0, theta_factor;
  int sample_sum;
  char sample_count;

  int snc = nc * SCVT_EXTRA_PRECISION_FACTOR; // scaled version of nc with extra pr

  // -------------------- INDEX FROM SCRATCH --------------------

  first_row_offset = - ((int)(first_angle / 2 / M_PI * nr) % nr);

  /* we'll need a list big enough to hold up to 4 input slot indexes per output slot */
      
  int inds_needed = w * h * 4;
  if (!inds || num_inds < inds_needed) {
    inds = new int[inds_needed];
    inds_alloc = inds_needed;
  }

  num_inds = 0;

  /* if a change of one pixel in the x direction causes a change of
     more than one along the scan row (i.e. samples are represented
     by less than one pixel) then we will average 3
     radially-neighbouring samples */

  use_radial_neighbours = scale < 1.0;  

  first_range *= scale; /* convert first_range into pixel units */

  scale /= SCVT_EXTRA_PRECISION_FACTOR; /* from now on, scale is scaled by extra precision bits */

  /* if a change of one pixel in the y direction causes a change of
     more than one scan row, then we will average 3
     angularly-neighbouring samples; we represent this in terms of
     the minimum sample range at and beyond which no such averaging is
     done */

  angular_neighbour_thresh = (int) (always_smooth_angular ?  nc * scale : (1 + nr / (2 * M_PI * scale)));

  l = 0; /* avoid a compiler warning */
  jhi = x0 + h;
  ihi = y0 + w; 
  theta0 = 2 * M_PI - first_angle;
  theta_factor = nr / (2 * M_PI);

  for (j = x0; j < jhi; ++j ) {
    y = - (j - yc + 0.5);
    for (i = y0; i < ihi; ++i) {
      x = i - xc + 0.5;
      theta = ((int) (0.5 + theta_factor * (atan2(x, y) + theta0))) % (unsigned) nr;
      range = (int) (0.5 + (sqrt(x * x + y * y) - first_range) / scale);
      if (range >= 0 && range < snc) {
        // the pixel has at least one corresponding data sample
        l = theta * snc + range;
        sample_sum = sample_count = 0;
        // use up to three neighbours
#ifdef DO_SCAN_CONVERSION_SMOOTHING
        if (range < angular_neighbour_thresh) {
          if (use_radial_neighbours && range <= snc - 2 * SCVT_EXTRA_PRECISION_FACTOR) {
            // radial, angular, and "diagonal" neighbour
            SCVT_IND(l + SCVT_EXTRA_PRECISION_FACTOR);
            if (theta > 0) {
              SCVT_IND(l - snc);
              SCVT_IND(l + SCVT_EXTRA_PRECISION_FACTOR - snc);
            } else {
              SCVT_IND(l + (nr - 1) * snc);
              SCVT_IND(l + SCVT_EXTRA_PRECISION_FACTOR + (nr - 1) * snc);
            }
          } else {
            // just angular neighbour
            if (theta > 0) {
              SCVT_IND(l - snc);
            } else {
              SCVT_IND(l + (nr - 1) * snc);
            }
          }
        } else {
          if (use_radial_neighbours) {
            if (range <= snc - 2 * SCVT_EXTRA_PRECISION_FACTOR)
              // just radial neighbour
              SCVT_IND(l + SCVT_EXTRA_PRECISION_FACTOR);
          }
        }
        // use the central sample, and mark it as the last for this pixel
        SCVT_IND_LAST(l);
#else  // DO_SCAN_CONVERSION_SMOOTHING
        SCVT_IND(l);
#endif // DO_SCAN_CONVERSION_SMOOTHING

      } else { // no corresponding radar data, so mark it as using no samples (it retains background colour)
        SCVT_NO_IND;
      }
    }
  }
};


scan_converter::~scan_converter() {
  if (inds) {
    delete [] inds;
    inds = 0;
  };
};


void
scan_converter::apply (t_sample *samp, 
                       t_pixel *pix,
                       int span,
                       t_palette *pal,
                       int sample_shift
                       ) {

/*
   fill an image (sub)window from polar data using a scan converter

   samp		: first sample in first row of polar input data
   pix	        : pointer to first pixel in the full output image (not the actual subimage being filled)
   span		: total pixels per image buffer row; this is used as the change in address
                  from the start of one sub-buffer line to the next.
   pal	        : pointer to palette array
   sample_shift : number of bits to shift sample value right before looking up in palette
*/

  int i, j, k;
  int palind;
  int mask; 
#ifdef DO_SCAN_CONVERSION_SMOOTHING
  int sample_sum;
  char sample_count;
#endif

  // addjust the pixel buffer pointer to the start of the subimage
  pix += x0 + y0 * span;

  // convenience variables
  k = w;

  // a mask for the final colour value (in case t_sample is signed and includes negative values) 
  mask = (1 << (8 * sizeof(t_sample) - sample_shift)) - 1; 

  // apply the sparse linear map

#ifdef DO_SCAN_CONVERSION_SMOOTHING
  sample_sum = sample_count = 0;
#endif

  for (i = 0, j = 0; i < num_inds; ++i) {
#ifdef DO_SCAN_CONVERSION_SMOOTHING
    if (inds[i] >= 0) {
      sample_sum += samp[inds[i] >> SCVT_EXTRA_PRECISION_BITS];
      ++sample_count;
    } else { 
#endif
      if (inds[i] != SCVT_NODATA_VALUE) {
	// a negative index represents the last one for
	// its pixel, so compute the mean and lookup the colour from the
	// palette for its class.
#ifdef DO_SCAN_CONVERSION_SMOOTHING
        // note: rather than divide by sample_count + 1, we shift right by ((sample_count + 1) / 2)
        // This works because sample_count is 0, 1, or 3 corresponding to 1, 2, or 4 samples being averaged.
        palind = ((((sample_sum + samp[ (~ inds[i]) >> SCVT_EXTRA_PRECISION_BITS]) >> ((sample_count + 1) >> 1)) >> sample_shift) & mask);
#else
        palind = ((((samp[inds[i] >> SCVT_EXTRA_PRECISION_BITS])) >> sample_shift) & mask);
#endif
#ifdef DO_ALPHA_BLENDING
        INLINE_ALPHA_BLEND(pal[palind], pix[j]);
#else
        pix[j] = pal[palind];
#endif
#ifdef DO_SCAN_CONVERSION_SMOOTHING
	sample_sum = sample_count = 0;
#endif
      } else {
	// This is a pixel for which no data value exists;
	// its existing value is preserved.

	/* do nothing */
      }
      // we're finished with the current pixel
      if (++j == k) {
	// we've finished a row, so do alpha blending
	// start the next image row

	j = 0;
	pix += span;
      }
#ifdef DO_SCAN_CONVERSION_SMOOTHING
    }
#endif
  }
};
