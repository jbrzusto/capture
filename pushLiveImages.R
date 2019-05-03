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

tmpDir = "/radar_temp"

## spool folder for latest radar images

spoolFolder = "/radar_spool/latest_images"

## desired pixels per metre
ppm = 1.0 / 4.8

## Azimuth and Range Offsets: if the heading pulse is flaky, azimuth offset must
## be used to set the orientation - in radians.  This can be changed
## dynamically by putting a numeric value (in degrees) in the file
## azimuthOffset.txt in the working directory, as is done for palette.
## 3rd, 4th elts are Offset of NE corner of image from radar, in metres [N, E].
## Increasing these values shifts the coverage to the NE.

## before 2015-11-26T12-39-00:
aziRangeOffsets = c(46.8,0,2200,500)

## Paul Bell's correction, which doesn't seem right: aziRangeOffsets = c(49.5,0,2200,500)

## template for copying JPG image and metadata, ensuring remote dir is created
scpCommandTemplate = "ssh -p 30022 -oControlMaster=auto -oControlPath=/tmp/ssh.force radar@radarcam.ca mkdir /volume1/all/radar/fvc/jpg/%s;\
scp -P 30022 -oControlMaster=auto -oControlPath=/tmp/ssh.force %s radar@radarcam.ca:/volume1/all/radar/fvc/jpg/%s &&\
scp -P 30022 -oControlMaster=auto -oControlPath=/tmp/ssh.force %s/FORCERadarSweepMetadata.txt radar@radarcam.ca:/volume1/all/radar/fvc/"

## removal zone - range of azimuths to drop from image
removal = NULL

argv = commandArgs(TRUE)

## should we ignore timestamps in moving files?
## normally we don't move files older than a minute.
## but why?

IGNORE_TS = FALSE

## push existing images, in case sending failed?

EXISTING_ONLY = FALSE

## only spool images, don't push them to discovery

SPOOL_ONLY = FALSE

while (length(argv) > 0) {
    switch (argv[1],
            "--remove" = {
                removal = as.numeric(strsplit(argv[2], ":")[[1]])
                argv = argv[-(1:2)]
            },
            "--incoming" = {
                INCOMING = argv[2]
                argv = argv[-(1:2)]
            },
            "--ignore_ts" = {
                IGNORE_TS = TRUE
                argv = argv[-1]
            },
            "--existing_only" = {
                EXISTING_ONLY = TRUE
                argv = argv[-1]
            },
            "--spool_only" = {
                SPOOL_ONLY = TRUE
                argv = argv[-1]
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
#xlim = c(-6000, 0)
xlim = c(-9000, 0)
iwidth = round(diff(xlim) * ppm)
##  xlim is north/south (negative = south)
ylim = c(-5775, 3182)
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

library(jpeg)
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

## start the inotifywait command, which will report events in the radar output directory

if (EXISTING_ONLY) {
    evtCon = pipe(sprintf("cd %s; find . -maxdepth 1 -type f -printf '%%f\n'", INCOMING), "r")
} else {
    evtCon = pipe(paste("/usr/bin/inotifywait -q -m -e close_write,moved_to --format %f", INCOMING), "r")
}

ni = 0
while (TRUE) {
    while (TRUE) {
	line = readLines(evtCon, n=1)
	if (length(line) == 0) {
	    if (EXISTING_ONLY) {
               quit("no")
            }
        } else {
            f = file.path(INCOMING, line)
            if (isTRUE(file.exists(f)))
                break
        }
        Sys.sleep(0.2)
    }

    tryCatch({
        con = file(f, "rb")
        hdr = readLines(con, n=2)

        if (!isTRUE(hdr[1] == "DigDar radar sweep file")) {
            cat("Skipping bogus file ", f, "\n")
            Sys.sleep(0.1)
            close(con)
            next
        }
        meta = fromJSON(hdr[2])
        if (! IGNORE_TS && as.numeric(Sys.time()) - meta$ts0 > 60) {
            close(con)
            next
        }
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
        rm(con)
        ## move the file to the spool folder, from where it will get filed

        cat(f, system(sprintf("mv %s %s", f, file.path("/radar_spool", basename(f)))), "\n")

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

        .Call("apply_scan_converter", scanConv, samples, pix, pal, as.integer(c(iwidth, 8192*decimation, 0.5 + decimation * (16383-8192) / 255)))

        jpgName = file.path(tmpDir, sub("dat$", "jpg", basename(f)))
        jpgFile = file(jpgName, "wb")
        writeJPEG(pix, jpgFile, quality=0.5, bg="black")
        close(jpgFile)

        ## make hardlink in spoolFolder, which must be on same drive
        bn = basename(jpgName)
        file.link(jpgName, file.path(spoolFolder, bn))

        ## Copy file to server
        if (! SPOOL_ONLY) {
            ## pull out YYYY-MM-DD
            u = regexpr("([0-9]{4}-[0-9]{2}-[0-9]{2})", bn)
            dateString = substring(bn, u, u+9)
            if (0 == system(sprintf(scpCommandTemplate, dateString, ## make remote dir
                                    jpgName, dateString, ## copy jpg filejpgName,
                                    tmpDir ## copy metadata file
                                    ))) {
                file.remove(jpgName)
            }
        }
    }, error=function(e) print(e)
    )
}
