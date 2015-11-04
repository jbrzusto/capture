#!/usr/bin/Rscript
##
## export N consecutive sweeps starting at the specified time, or the
## current time if none is specified, truncated down to a multiple of
## M minutes, then bzip2 the file and send it to the FORCE server.

## number of consecutive sweeps to export at each time step
N = 257

## minutes per timestep
M = 30

## destination user, host, address folder for .pol files
SCP_DEST = "radar_upload@force:/mnt/raid1/radar/fvc"

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

## if user specified a date/time on command line, use that; otherwise,
## use most recently completed time period.

ts = commandArgs(TRUE)[1]
if (is.na(ts)) {
    now = Sys.time()
} else {
    now = ymd_hms(ts)
}

topOfDay = trunc(now, "days")

minutes = floor(diff(as.numeric(c(topOfDay, now))) / 60)

lastStepStart = floor((minutes - M) / M) * M

start = topOfDay + 60 * lastStepStart

startHour = format(start, "%Y-%m-%d/%H")

sourceFolder = hours$path[match(startHour, hours$dateHour)]

if (is.na(sourceFolder)) {
    stop("Couldn't find data starting at", start)
}

f = dir(sourceFolder, full.names=TRUE)

fts = do.call(data.frame,attributes(regexpr(".*([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}-[0-9]{2}-[0-9]{2}\\.[0-9]{6})", f, perl=TRUE)))

fts = ymd_hms(substr(f, fts[,3], fts[,3] + fts[, 4]))

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
}

## get depths at each frame

depth = getDepth(as.numeric(fts))

outname = exportWamos(sweeps, path="/tmp", depths=depth, nACP=450, aziLim=c(0.12, 0.43),rangeLim=c(0,3000), decim=3)
bzName = paste(outname, ".bz2", sep="")

## compress file; copy to FORCE workstation; delete
system(paste("bzip2 -9", outname, "; scp -oControlMaster=no -q -i ~/.ssh/id_dsa_vc_radar_laptop", bzName, SCP_DEST, ";", "rm -f", bzName))




