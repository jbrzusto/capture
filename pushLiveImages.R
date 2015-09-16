#!/usr/bin/R -f

## pushLiveImages.R: generate an image for each sweep of committed data and push
## it to the webserver.

## Each time a sweep of data has been committed to the database by the sqlite
## capture program, generate a scan-converted image and push that and the
## timestamp of the last pulse in the image to the web server.
## This program must be estarted if the capture program is, as it opens
## a connection to the most recent sqlite database, and does not attempt
## to detect its closure and the start of a new one.

## -------------------- USER OPTIONS --------------------

## each option can be replaced by a value specified on the command line

tmpDir = "/tmp"

## Overlay Image dimensions: we generate a square image, this many pixels
##   on a side.

imageSize = 1024L

## desired pixels per metre
ppm = 1.0 / 7.5

## Azimuth and Range Offsets: if the heading pulse is flaky, azimuth offset must
## be used to set the orientation - in radians.  This can be changed
## dynamically by putting a numeric value (in degrees) in the file
## azimuthOffset.txt in the working directory, as is done for palette.
## 3rd, 4th elts are Offset of NE corner of image from radar, in metres [N, E].
## Increasing these values shifts the coverage to the NE.

aziRangeOffsets = c(46.8,0,2200,500)

## SCP Destination User / Host to which images are pushed via secure copy
scpDestUser = "force-radar@discovery"

## SCP Destination - folder on remote host to which images are pushed
scpDestDir = "/home/www/html/htdocs/force/"

## archiveScript - script on remote host for archiving uploaded image
archiveScript = "/home/john/proj/force_radar_website/archive_image.py"

## removal zone - range of azimuths to drop from image
removal = NULL

argv = commandArgs(TRUE)

while (length(argv) > 0) {
    switch (argv[1],
            "--remove" = {
                removal = as.numeric(strsplit(argv[2], ":")[[1]])
                argv = argv[-(1:2)]
            },
            {
                stop("Unknown option", argv[1])
            }
            )
}
            
## -------------------- END OF USER OPTIONS --------------------

##
VELOCITY_OF_LIGHT = 2.99792458E8

## Desired image extents
##  xlim is east/west (negative = west)
xlim = c(-8594, 0)
iwidth = round(diff(xlim) * ppm)
##  xlim is north/south (negative = south)
ylim = c(-5775, 2182)
iheight = round(diff(ylim) * ppm)

## desired azimuths
if (is.null(removal)) {
    desiredAzi = seq(from=0, to=1, by = 1.0 / 3600)
} else if (diff(removal) > 0) {
    desiredAzi = c(seq(from=0, to = removal[1], by = 1.0/3600), seq(from = removal[2], to = 1, by = 1.0/3600))
} else {
    desiredAzi = seq(from=removal[2], to=removal[1], by=1.0 / 3600)
}    

## Pulses per sweep: a kludgy way to achieve a fixed number of pulses
##   per sweep, currently needed by the scan converter.  The
##   Bridgemaster E operating in short pulse mode generates pulses @
##   1800 Hz and rotates at 28 RPM for a total of ~ 3857 pulses per
##   sweep. We select down to 3600 pulses, which gives 0.1 degree
##   azimuth resolution.

pulsesPerSweep = length(desiredAzi)

##library(jpeg)
library(png)
library(jsonlite)
dyn.load("/home/radar/capture/capture_lib.so")


## the capture process write its filenames to stdout,
## and we read this from stdin

pix = matrix(0L, iheight, iwidth)
class(pix)="nativeRaster"
attr(pix, "channels") = 4

scanConv = NULL
sk = 0

pal = readRDS("/home/radar/capture/radarImagePalette.rds")  ## low-overhead read of palette, to allow changing dynamically
options(digits=14)
fcon = file("/dev/stdin", "r")

while (TRUE) {
  gc(verbose=FALSE)
  f = readLines(fcon, n=1)
## e.g.  f="/media/FORCEradar9/2015-09-16/06/FORCEVC-2015-09-16T06-05-50.979752.dat"
  if (length(f) == 0 || ! isTRUE(file.exists(f))) {
      cat("Skipping missing file ", f, "\n")
      Sys.sleep(0.1)
      next
  }
  con = file(f, "rb")
  hdr = readLines(con, n=2)

  if (hdr[1] != "DigDar radar sweep file") {
      cat("Skipping bogus file ", f, "\n")
      Sys.sleep(0.1)
      close(con)
      next
  }
  cat("Reading file ", f, "\n")
  meta = fromJSON(hdr[2])

  samplesPerPulse = meta$ns
  samplingRate = meta$clock * 1e6
  decimation = meta$decim
  
  ## metres per sample
  mps = VELOCITY_OF_LIGHT / (samplingRate / decimation) / 2.0

  x = data.frame(
      clocks  = readBin(con, integer(), n = meta$np, size=4),
      azi     = readBin(con, numeric(), n = meta$np, size=4),
      trigs   = readBin(con, integer(), n = meta$np, size=4)
      )
  samples = readBin(con, raw(), n = meta$np * meta$ns * 2)
  dim(samples) = c(meta$ns * 2, meta$np)
  close(con)

  ## get pulses uniformly spread around circle

  keep = approx(x$azi,1:nrow(x),desiredAzi, method="constant", rule=2)$y
  
  x = x[keep,] ## pulses are rows
  samples = samples[,keep] ## yes, different index slot than previous line: pulses are columns
  
  ## output timestamp of last pulse, and azi/range offsets
  metaCon = file(file.path(tmpDir, "FORCERadarSweepMetadata.txt"), "w")
  cat(sprintf("{\n  \"ts\": %.3f,\n  \"samplesPerPulse\": %d,\n  \"pulsesPerSweep\": %d,\n  \"width\": %d,\n   \"height\": %d,\n  \"xlim\": [%f, %f],\n   \"ylim\": [%f, %f],\n  \"ppm\": %f,\n \"aziOffset\": %f,\n  \"rangeOffset\": %f,\n  \"samplingRate\": %f\n}", meta$ts0, samplesPerPulse, pulsesPerSweep, iwidth, iheight, xlim[1], xlim[2], ylim[1], ylim[2], ppm, aziRangeOffsets[1], aziRangeOffsets[2], samplingRate / decimation ), file=metaCon)
  close(metaCon)

  ## if necessary, regenerate scan converter
  if (is.null(scanConv)) {
    
    scanConv = .Call("make_scan_converter", as.integer(c(pulsesPerSweep, samplesPerPulse, iwidth, iheight, 0, 0, iwidth, ylim[2] * ppm, TRUE)), c(ppm * mps, aziRangeOffsets[2] , aziRangeOffsets[1]/360+desiredAzi[1], aziRangeOffsets[1]/360+tail(desiredAzi,1)))
}

  .Call("apply_scan_converter", scanConv, samples, pix, pal, as.integer(c(iwidth, 6100*decimation, 0.5 + decimation * (16383-6100) / 255)))

  ## Note: write PNG to newFORCERadarImage.png, then rename to currentFORCERadarImage.png so that
  ##
  pngFile = file(file.path(tmpDir, "currentFORCERadarImage.png"), "wb")
  writePNG(pix, pngFile)
  close(pngFile)
  ## jpgFile = file(file.path(tmpDir, "currentFORCERadarImage.jpg"), "wb")
  ## writeJPEG(pix, jpgFile, quality=0.5, bg="black") ## this is actually sufficient!
  ## close(jpgFile)
  ## Copy file to server, then rename 'current' to plain version; this
  ## is done atomically, so that if the web server process is serving
  ## the file, it serves either the complete previous image, or the
  ## complete new image, rather than a partial or corrupt image.
  system(sprintf("scp -q %s/currentFORCERadarImage.png %s/FORCERadarSweepMetadata.txt %s:%s; ssh %s '%s'", tmpDir, tmpDir, scpDestUser, scpDestDir, scpDestUser, archiveScript))
}

