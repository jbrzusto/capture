/**
 * @file capture_lib.c
 *  
 * @brief functions for handling live capture of radar data from R
 * 
 * @author John Brzustowski <jbrzusto is at fastmail dot fm>
 * @version 0.1
 * @date 2013
 * @license GPL v3 or later
 *
 */

#define USE_RINTERNALS
#include "R.h"
#include "Rinternals.h"

#include "scan_converter.h"

scan_converter * _make_scan_converter (int nr,
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
                                       )
{
  return new scan_converter(nr, nc, w, h, x0, y0, xc, yc, always_smooth_angular, scale, first_range, azi_begin, azi_end);
};

void _delete_scan_converter (scan_converter *sc) {
  delete sc;
};

void _apply_scan_converter (scan_converter *sc, 
                            t_sample *samp, 
                            t_pixel *pix,
                            int span,
                            t_palette *pal,
                            int sample_origin,
                            int sample_scale)
{
  sc->apply(samp, pix, span, pal, sample_origin, sample_scale);
};

extern "C" {
#include <R_ext/Visibility.h>
#include <R_ext/Rdynload.h>
#include "R.h"
#include "Rinternals.h"
#include "latest_pulse_timestamp.h"

SEXP
get_latest_pulse_timestamp() {
  return ScalarReal(latest_pulse_timestamp());
}

SEXP
make_scan_converter (SEXP int_args, SEXP double_args) {
  scan_converter * sc = _make_scan_converter (
                        INTEGER(int_args)[0],
                        INTEGER(int_args)[1],
                        INTEGER(int_args)[2],
                        INTEGER(int_args)[3],
                        INTEGER(int_args)[4],
                        INTEGER(int_args)[5],
                        INTEGER(int_args)[6],
                        INTEGER(int_args)[7],
                        INTEGER(int_args)[8],
                        REAL(double_args)[0],
                        REAL(double_args)[1],
                        REAL(double_args)[2],
                        REAL(double_args)[3]
                                              );
  return R_MakeExternalPtr(sc, 0, 0);
};

SEXP 
delete_scan_converter (SEXP sc_handle) {
  _delete_scan_converter ((scan_converter *) EXTPTR_PTR(sc_handle));
};

SEXP
apply_scan_converter (SEXP sc_handle, SEXP samples, SEXP pixels, SEXP palette, SEXP int_args) {
  scan_converter * scp = (scan_converter *) EXTPTR_PTR(sc_handle);
  _apply_scan_converter(scp, (unsigned short *) RAW(samples), (unsigned int *) INTEGER(pixels), INTEGER(int_args)[0], (unsigned int *) INTEGER(palette), INTEGER(int_args)[1], INTEGER(int_args)[2]);
  return R_NilValue;
};

#define MKREF(FUN, N) {#FUN, (DL_FUNC) &FUN, N}

R_CallMethodDef capture_lib_call_methods[]  = {
  MKREF(get_latest_pulse_timestamp,  0),
  MKREF(make_scan_converter, 2),
  MKREF(delete_scan_converter, 1),
  MKREF(apply_scan_converter, 5),
  {NULL, NULL, 0}
};

void
R_init_capture_lib(DllInfo *info)
{
  /* Register routines, allocate resources. */

  // set RPC timeout so that loading the seascan library
  // doesn't take forever
  R_registerRoutines(info, NULL, capture_lib_call_methods, NULL, NULL);
}

void
R_unload_capture_lib(DllInfo *info)
{
}

};
