#!/usr/bin/Rscript
##
## export N consecutive sweeps starting at the specified time, or the
## current time if none is specified, truncated down to a multiple of
## M minutes, then bzip2 the file and send it to the FORCE server.

Sys.setenv(TZ="GMT")

## number of consecutive sweeps to export at each time step
#N = 257
N = 129

## minutes per timestep
#M = 30
M = 15

## length of time used per timestep, in minutes
#SLEN = 10
SLEN = 5

## destination user, host, address folder for .pol files
SCP_DEST = "radar_upload@force:data"

###########################################################################
##
## summary image options
##
##
## desired pixels per metre
ppm = 1.0 / 4.8

## Azimuth and Range Offsets: if the heading pulse is flaky, azimuth offset must
## be used to set the orientation - in radians.  This can be changed
## dynamically by putting a numeric value (in degrees) in the file
## azimuthOffset.txt in the working directory, as is done for palette.
## 3rd, 4th elts are Offset of NE corner of image from radar, in metres [N, E].
## Increasing these values shifts the coverage to the NE.

aziRangeOffsets = c(46.8,0,2200,500)
## Desired image extents
##  xlim is east/west (negative = west)
#xlim = c(-6000, 0)
xlim = c(-9000, 0)
iwidth = round(diff(xlim) * ppm)
##  xlim is north/south (negative = south)
ylim = c(-5775, 3182)
iheight = round(diff(ylim) * ppm)

MaxRange = max(abs(c(xlim, ylim)))
library(jpeg)
dyn.load("/home/radar/capture/capture_lib.so")

pix = matrix(0L, iheight, iwidth)
class(pix)="nativeRaster"
attr(pix, "channels") = 4

scanConv = NULL
sk = 0

pal = readRDS("/home/radar/capture/radarImagePalette.rds")  ## low-overhead read of palette, to allow changing dynamically

##
##
###############################################################

## directory where ongoing radar storage is mounted
## each disk is mounted in a subfolder, then sweeps are stored
## in subfolders two levels down with paths %Y-%m-%d/%H
RADAR_STORE = "/mnt/radar_storage"

## regex matching sweep files
SWEEP_FILE_REGEX = "\\.dat$"

## get the drives used for storage
drives = dir(RADAR_STORE, full.names=TRUE, pattern="^sd.*$")

## get the list of day folders
days = dir(drives, full.names=TRUE, pattern="^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")

## get the list of hour folders
hours = dir(days, full.names=TRUE, pattern="^[0-9][0-9]$")
hours = data.frame(path = I(hours), dateHour = I(file.path(basename(dirname(hours)), basename(hours))))

## get current time, calculate start of most recent complete time
## period, and find it on disk
## FIXME: should be keeping track of all files captured in an sqlite database

library(lubridate)
library(flow)
library(jsonlite)
library(XML)

## if user specified a date/time on command line, use that; otherwise,
## use most recently completed time period.

ts = commandArgs(TRUE)[1]
if (is.na(ts)) {
    now = Sys.time() - SLEN * 60
} else {
    now = ymd_hms(ts)
}

topOfDay = trunc(now, "days")

minutes = floor(diff(as.numeric(c(topOfDay, now))) / 60)

lastStepStart = floor(minutes / M) * M

start = topOfDay + 60 * lastStepStart

startHour = format(start, "%Y-%m-%d/%H")

## find folder(s) with matching hour

sourceFolder = hours$path[startHour==hours$dateHour]

if (is.na(sourceFolder)) {
    stop("Couldn't find data starting at", start)
}

f = dir(sourceFolder, full.names=TRUE)

fts = do.call(data.frame,attributes(regexpr(".*([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}-[0-9]{2}-[0-9]{2}\\.[0-9]{6})", f, perl=TRUE)))

fts = ymd_hms(substr(f, fts[,3], fts[,3] + fts[, 4]))
ord = order(fts)
f = f[ord]
fts = fts[ord]

## index of first file to use
use = which(as.numeric(fts) >= as.numeric(start))[1:N]
useFiles = f[use]
fts = fts[use]

if (any(is.na(useFiles))) {
    stop("insufficient files for export, starting at ", format(start))
}

## read sweeps
sweeps = vector("list", N)

VELOCITY_OF_LIGHT = 2.99792458E8

for(i in seq(along=useFiles)) {
    ## read in sweep
    zcon = gzfile(useFiles[i], "rb")
    hdr = readLines(zcon, 2)
    if (! grepl("^DigDar", hdr[1]))
        stop("file is not a DigDar sweep file: ", useFiles[i])
    meta = fromJSON(hdr[2])
    ## read binary data from sweep
    b = readBin(zcon, raw(), n=meta$bytes)
    close(zcon)

    samplesPerPulse = meta$ns
    samplingRate = meta$clock * 1e6
    decimation = meta$decim
  
    ## metres per sample
    mps = VELOCITY_OF_LIGHT / (samplingRate / decimation) / 2.0

    con = rawConnection(b, "r")
    clocks = readBin(con, integer(), n = meta$np, size=4) / (meta$clock * 1e6)
    sweeps[[i]] = list (
        ts      = meta$ts0 - clocks[1] + clocks,
        azi     = readBin(con, numeric(), n = meta$np, size=4),
        trigs   = readBin(con, integer(), n = meta$np, size=4),
        samples = readBin(con, raw(), n = meta$np * meta$ns * 2)
    )
    meta$rate = meta$clock * 1e6 / meta$decim
    attr(sweeps[[i]], "radar.meta") = meta
    close(con)
    if (i == 1) {
        ## create a summary image from this sweep
        x = sweeps[[1]]
        dim(x$samples) = c(meta$ns * 2, meta$np)
        
        samplingRate = meta$clock * 1e6
        decimation = meta$decim
        
        ## metres per sample
        mps = VELOCITY_OF_LIGHT / (samplingRate / decimation) / 2.0

        ## azimuth range of valid pulses at 0.1 deg spacing


        ## Pulses per sweep: a kludgy way to achieve a fixed number of pulses
        ##   per sweep, currently needed by the scan converter.  The
        ##   Bridgemaster E operating in short pulse mode generates pulses @
        ##   1800 Hz and rotates at 28 RPM for a total of ~ 3857 pulses per
        ##   sweep. We select down to 3600 pulses, which gives 0.1 degree
        ##   azimuth resolution.

        desiredAzi = seq(from = 0.12, to = 0.43, by = 1.0 / 3600)
        pulsesPerSweep = length(desiredAzi)

        ## get pulses uniformly spread around circle
        
        keep = approx(x$azi, seq(along=x$azi), desiredAzi, method="constant", rule=2)$y
        
        x$samples = x$samples[,keep]
        
        scanConv = .Call("make_scan_converter", as.integer(c(pulsesPerSweep, meta$ns, iwidth, iheight, 0, 0, iwidth, ylim[2] * ppm, TRUE)), c(ppm * mps, aziRangeOffsets[2] , aziRangeOffsets[1]/360+desiredAzi[1], aziRangeOffsets[1]/360+tail(desiredAzi,1)))
    
        .Call("apply_scan_converter", scanConv, x$samples, pix, pal, as.integer(c(iwidth, 8192 * decimation, 0.5 + decimation * (16383 - 8192) / 255)))
    }
}


## get depths at each frame

depth = getDepth(as.numeric(fts))

## export as Wamos file
outname = exportWamos(sweeps, path="/tmp", depths=depth, nACP=450, aziLim=c(0.12, 0.43),rangeLim=c(0,MaxRange), decim=3)

## write out jpeg
outnameStem = sub(".pol", "", outname, fixed=TRUE)

jpgFile = paste(outnameStem, ".jpg", sep="")
writeJPEG(pix, jpgFile, quality=0.5, bg="black")

bzName = paste(outname, ".bz2", sep="")

## compress file; copy to FORCE workstation; delete
system(paste("bzip2 -9", outname, "; if ( scp -oControlMaster=no -oControlPath=none -i ~/.ssh/id_dsa_vc_radar_laptop", paste(outnameStem, "*", sep=""), SCP_DEST, ") then", "rm -f", paste(outnameStem, "*", sep=""), "; fi "))





