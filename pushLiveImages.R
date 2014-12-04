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

## each option can be replaced by a value specified on the command line

## Directory where the capture program stores its sqlite database files

##dbDir = "/mnt/3tb/force_data"
dbDir = "/media/FORCE_radar_1/"

## Samples per pulse: set by the capture program script; at the full
##   digitizing rate of 125 MHz, range per sample is 1.2 metres, so
##   1664 samples takes us out to 3.6 km.  The capture program
##   script sets the samplesPerPulse.

#samplesPerPulse = 1664L
samplesPerPulse = 2000L

## Sampling Rate: base clock rate for samples.

samplingRate = 125e6

## Decimation rate: actual sample clock rate is obtained by
## dividing samplingRate by decim.

#decimation = 3
decimation = 4

## Overlay Image dimensions: we generate a square image, this many pixels
##   on a side.

imageSize = 1536L

## Azimuth and Range Offsets: if the heading pulse is flaky, azimuth offset must
## be used to set the orientation - in radians.  This can be changed
## dynamically by putting a numeric value (in degrees) in the file
## azimuthOffset.txt in the working directory, as is done for palette.
## 3rd, 4th elts are Offset of NE corner of image from radar, in metres [N, E].
## Increasing these values shifts the coverage to the NE.

aziRangeOffsets = c(46.8,0,2200,500)

## Azimuth offset file: allows live correction of azimuth angle.  This
## is a temporary kludge!
aziRangeOffsetsFile = "/home/radar/capture/aziRangeOffsets.txt"

## SCP Destination User / Host to which images are pushed via secure copy
scpDestUser = "force-radar@discovery"

## SCP Destination - folder on remote host to which images are pushed
scpDestDir = "/home/www/html/htdocs/force/"

## archiveScript - script on remote host for archiving uploaded image
archiveScript = "/home/john/proj/force_radar_website/archive_image.py"

argv = commandArgs(TRUE)

while (length(argv) > 0) {
    switch (argv[1],
            "--dbdir" = {
                dbDir = argv[2]
                argv = argv[-(1:2)]
            },
            "--pulses" = {
                pulsesPerSweep = as.numeric(argv[2])
                argv = argv[-(1:2)]
            },
            "--samples" = {
                samplesPerPulse = as.numeric(argv[2])
                argv = argv[-(1:2)]
            },
            "--image_size" = {
                imageSize = as.numeric(argv[2])
                argv = argv[-(1:2)]
            },
            "--azi_offset" = {
                aziRangeOffsets[1] = as.numeric(argv[2])
                argv = argv[-(1:2)]
            },
            "--range_offset" = {
                aziRangeOffsets[2] = as.numeric(argv[2])
                argv = argv[-(1:2)]
            },
            "--sampling_rate" = {
                samplingRate = as.numeric(argv[2])
                argv = argv[-(1:2)]
            },
            "--decim" = {
                decimation = as.numeric(argv[2])
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

## metres per sample
mps = VELOCITY_OF_LIGHT / (samplingRate / decimation) / 2.0

## pixels per metre
ppm = imageSize / (2 * samplesPerPulse * mps)

## desired azimuths
desiredAzi = seq(from=0.122, to=0.425, by=1.0 / 3600)

## Pulses per sweep: a kludgy way to achieve a fixed number of pulses
##   per sweep, currently needed by the scan converter.  The
##   Bridgemaster E operating in short pulse mode generates pulses @
##   1800 Hz and rotates at 28 RPM for a total of ~ 3857 pulses per
##   sweep. We select down to 3600 pulses, which gives 0.1 degree
##   azimuth resolution.

pulsesPerSweep = length(desiredAzi)

## Get all database filenames 

dbFiles = dir(dbDir, pattern="^force.*\\.sqlite$", full.names=TRUE)

## choose the most recent one
dbFile = dbFiles[order(file.info(dbFiles)$mtime, decreasing=TRUE)[1]]

dyn.load("/home/radar/capture/capture_lib.so") 
library(RSQLite)
library(png)

## loop for a while, trying to connect; the db is initially locked by rpcapture

con = NULL
while (is.null(con)) {
    tryCatch (
        {
            con = dbConnect("SQLite", dbFile)
        },
        error = function(e) {
            Sys.sleep(1)
        }
        )
}

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
  
##  ts = .Call("get_latest_pulse_timestamp") ## this is an atomic read from semaphore-protected shared memory

  ## get the key for the sweep before the one being filled now
  sk = dbGetQuery(con, sprintf("select distinct sweep_key from pulses order by sweep_key desc limit 2", ts))[2, 1]
  
  if (! isTRUE(sk != last.sk && sk > 0))
    next
  
  last.sk = sk

  pal = readRDS("/home/radar/capture/radarImagePalette.rds")  ## low-overhead read of palette, to allow changing dynamically

  ## get all pulses for this sweep
  x = dbGetQuery(con, sprintf("select * from pulses where sweep_key = %d order by ts", sk))
  options(digits=14)

  ## get pulses uniformly spread around circle

  keep = approx(x$azi,1:nrow(x),desiredAzi, method="constant", rule=2)$y
  
  x=x[keep,]
  b = unlist(x$samples) ## concatenate the raw bytes for all pulses into a single raw vector
  
  lastAziRangeOffsets = aziRangeOffsets
  if (file.exists(aziRangeOffsetsFile))
    aziRangeOffsets = scan(aziRangeOffsetsFile, sep=",", quiet=TRUE)

  ## output timestamp of last pulse, and azi/range offsets
  cat(sprintf("{\n  \"ts\": %.3f,\n  \"samplesPerPulse\": %d,\n  \"pulsesPerSweep\": %d,\n  \"imageSize\": %d,\n  \"aziOffset\": %f,\n  \"rangeOffset\": %f,\n  \"samplingRate\": %f\n}", tail(x$ts, 1), samplesPerPulse, pulsesPerSweep, imageSize, aziRangeOffsets[1], aziRangeOffsets[2], samplingRate / decimation ), file=file.path(dbDir, "FORCERadarSweepMetadata.txt"))

  ## if necessary, regenerate scan converter
  if (is.null(scanConv) || ! identical(aziRangeOffsets, lastAziRangeOffsets)) {
    if (! is.null(scanConv))
      .Call("delete_scan_converter", scanConv)
    
    scanConv = .Call("make_scan_converter", as.integer(c(pulsesPerSweep, samplesPerPulse, imageSize, imageSize, 0, 0, imageSize - aziRangeOffsets[4] * ppm, aziRangeOffsets[3] * ppm, TRUE)), c(imageSize / (2 * samplesPerPulse), aziRangeOffsets[2] , aziRangeOffsets[1]/360+desiredAzi[1], aziRangeOffsets[1]/360+tail(desiredAzi,1)))
}

  .Call("apply_scan_converter", scanConv, b, pix, pal, as.integer(c(imageSize, 7000*decimation, decimation * 64L)))

  ## Note: write PNG to newFORCERadarImage.png, then rename to currentFORCERadarImage.png so that
  ##
  pngFile = file(file.path(dbDir, "currentFORCERadarImage.png"), "wb")
  writePNG(pix, pngFile)
  close(pngFile)
  system(sprintf("scp -q %s/currentFORCERadarImage.png %s/FORCERadarSweepMetadata.txt %s:%s", dbDir, dbDir, scpDestUser, scpDestDir))
  ## rename 'current' to plain version; this is done atomically, so that if the web server
  ## process is serving the file, it serves either the complete previous image, or the complete new
  ## image, rather than a partial or corrupt image.
  
  system(sprintf("ssh %s %s", scpDestUser, archiveScript))
} 
