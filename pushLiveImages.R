#!/usr/bin/Rscript

## pushLiveImages.R: generate an image for each sweep of committed data and push
## it to the webserver.

## Each time a sweep of data has been committed to the database by the sqlite
## capture program, generate a scan-converted image and push that and the
## timestamp of the last pulse in the image to the web server.
## This program must be estarted if the capture program is, as it opens
## a connection to the most recent sqlite database, and does not attempt
## to detect its closure and the start of a new one.

## -------------------- USER OPTIONS --------------------
## Directory where the capture program stores its sqlite database files

dbDir = "/mnt/3tb/force_data"

## Pulses per sweep: a kludgy way to achieve a fixed number of pulses
##   per sweep, currently needed by the scan converter.  The
##   Bridgemaster E operating in short pulse mode generates pulses @
##   1800 Hz and rotates at 28 RPM for a total of ~ 3857 pulses per
##   sweep.

pulsesPerSweep = 3980L

## Samples per pulse: set by the capture program script; at the full
##   digitizing rate of 64 MHz, range per sample is 2.342 metres, so
##   1024 samples takes us out to 2.398 km.  The capture program
##   script sets the samplesPerPulse.

samplesPerPulse = 1024L

## Overlay Image dimensions: we generate a square image, this many pixels
##   on a side.  Note that this many pixels corresponds to 2 * samplesPerPulse,
##   because the square image contains a circle of radius samplesPerPulse.

imageSize = 1024L

## Azimuth and Range Offsets: if the heading pulse is flaky, azimuth offset must
## be used to set the orientation - in radians.  This can be changed
## dynamically by putting a numeric value (in degrees) in the file
## azimuthOffset.txt in the working directory, as is done for palette.

aziRangeOffsets = c(164 * (pi / 180), 40)

## Azimuth offset file: allows live correction of azimuth angle.  This
## is a temporary kludge!
aziRangeOffsetsFile = "/home/radar/capture/aziRangeOffsets.txt"

## SCP Destination User / Host to which images are pushed via secure copy
scpDestUser = "force-radar@discovery.acadiau.ca"

## SCP Destination - folder on remote host to which images are pushed
scpDestDir = "/home/www/html/htdocs/force/"

## -------------------- END OF USER OPTIONS --------------------

## total samples per sweep
samplesPerSweep = as.integer(pulsesPerSweep * samplesPerPulse)

## Get all database filenames 

dbFiles = dir(dbDir, pattern="^force.*\\.sqlite$", full.names=TRUE)

## choose the most recent one
dbFile = dbFiles[order(file.info(dbFiles)$mtime, decreasing=TRUE)[1]]

dyn.load("/home/radar/capture/capture_lib.so") 
library(RSQLite)
library(png)
con = dbConnect("SQLite", dbFile)
pix = matrix(0L, imageSize, imageSize)
class(pix)="nativeRaster"
attr(pix, "channels") = 4

scanConv = NULL
last.sk = -1
while (TRUE) {
  gc(verbose=FALSE)
  Sys.sleep(0.1)
  ## see how far the pulse capturing has gone.
  ## For now, we look for the latest complete sweep.  For later, we'll generate images chunk by chunk (e.g. quadrant
  ## by quadrant) for finer-grained screen update.  The trick is not to query too close to the leading
  ## edge of data, as this can cause indefinite growth in the size of the sqlite write-ahead-log file and/or
  ## cache file(s).
  
  ts = .Call("get_latest_pulse_timestamp") ## this is an atomic read from semaphore-protected shared memory

  ## get the key for the sweep before the one being filled now
  sk = dbGetQuery(con, sprintf("select sweep_key from pulses where ts <= %f order by ts desc limit 1", ts))[1, 1] - 1
  
  if (sk == last.sk || sk < 1)
    next
  
  last.sk = sk

  pal = readRDS("/home/radar/capture/radarImagePalette.rds")  ## low-overhead read of palette, to allow changing dynamically

  ## get all pulses for this sweep; FIXME: this is a kludge - the scan converter requires a fixed number
  ## of input pulses, so we always ask for 3900 pulses, and fill in any extra by replicating the last
  x = dbGetQuery(con, sprintf("select * from pulses where sweep_key = %d order by ts limit %d", sk, pulsesPerSweep))
  options(digits=14)
  cat(tail(x$ts, 1), file="FORCERadarTimestamp.txt")
  
  b = unlist(x$samples) ## concatenate the raw bytes for all pulses into a single raw vector
  
  pulsesMissing = (samplesPerSweep - length(b) / 2) / samplesPerPulse ## each sample is 2 bytes

  if (pulsesMissing > 0) {
    ## KLUDGE: replicate the last pulse the required number of times
    b = c(b, rep(tail(b, 2 * samplesPerPulse), times=pulsesMissing))
  }

  lastAziRangeOffsets = aziRangeOffsets
  if (file.exists(aziRangeOffsetsFile))
    aziRangeOffsets = scan(aziRangeOffsetsFile, sep=",", quiet=TRUE)

  ## if necessary, regenerate scan converter
  if (is.null(scanConv) || ! identical(aziRangeOffsets, lastAziRangeOffsets)) {
    if (! is.null(scanConv))
      .Call("delete_scan_converter", scanConv)
    
    scanConv = .Call("make_scan_converter", as.integer(c(pulsesPerSweep, samplesPerPulse, imageSize, imageSize, 0, 0, imageSize / 2, imageSize / 2, TRUE)), c(imageSize / (2 * samplesPerPulse), aziRangeOffsets[1] * pi/180, aziRangeOffsets[2]))
  }

  .Call("apply_scan_converter", scanConv, b, pix, pal, c(imageSize, 4L))

  writePNG(pix, "currentFORCERadarImage.png")
  system(sprintf("scp -q currentFORCERadarImage.png %s:%s", scpDestUser, scpDestDir))
  system(sprintf("scp -q FORCERadarTimestamp.txt %s:%s", scpDestUser, scpDestDir))
  ## rename 'current' to plain version; this is done atomically, so that if the web server
  ## process is serving the file, it serves either the complete previous image, or the complete new
  ## image, rather than a partial or corrupt image.
  
  system(sprintf("ssh %s mv -f %s/currentFORCERadarImage.png %s/FORCERadarImage.png", scpDestUser, scpDestDir, scpDestDir))
} 
