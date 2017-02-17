#!/usr/bin/Rscript
#
# annotate radar jpegs from a folder into another

library(flow)
library(jpeg)
library(XML)

options(digits=14, digits.secs=3)

TSC = class(Sys.time())
TS = function(x) structure(x, class=TSC)

ARGV = commandArgs(TRUE)

if (! length(ARGV) %in% c(4, 5)) {
    cat ("\
Usage: annotateMove.R JPGFOLDER DESTFOLDER START END [STEP]\n
\n
For each .JPG image in JPGFOLDER/YYYY-MM-DD between START and END\n
annotate with a timestamp, scale bar, and tide gauge.\n
Each .JPG image is resaved to DESTFOLDER, and the script outputs\n
the full path to each JPG image to stdout.\n
START and END are in any format recognized by lubridate, e.g.\n

   'YYYY-MM-DD HH-MM-SS'

")
    quit(save="no")
}

JPGFOLDER = ARGV[1]

isdir = function(x) file.exists(x) && file.info(x)$isdir

if (! isdir(JPGFOLDER))
    stop(JPGFOLDER, " is not a valid directory")

DESTFOLDER = ARGV[2]
if (! isdir(DESTFOLDER))
    stop(DESTFOLDER, " is not a valid directory")

RANGE = ymd_hms(ARGV[3:4])
if (!(all(is.finite(RANGE))) || diff(RANGE) < 0)
    stop(ARGV[3], " to ", ARGV[4], " is not a valid time range")

STEP = 1
if (length(ARGV) > 4)
    STEP = as.numeric(ARGV[5])

## get the hourly tide predictions for this location
## from the tides.gc.ca server, for the given time range
## Convert to cubic spline, to catch peaks and troughs.
## Then convert to an approximation function that maps timestamps
## to tide levels.
tide = approxfun(spline(predictTide(RANGE[1] - 12.5*3600, RANGE[2] + 12.5*3600, hourly=TRUE)))

## loop over daily folders

day = trunc(RANGE[1], "day")

## keep track of timestamp we're looking for
minTS = RANGE[1]
while (day < RANGE[2]) {
    SRCFOLDER = file.path(JPGFOLDER, format(day, "%Y-%m-%d"))
    if (! isdir(SRCFOLDER))
        break
    ff = dir(SRCFOLDER, pattern=".*.jpg$", full.names=TRUE)
    fts = basename(ff)
    fts = sub("force_", "", fts)
    fts = sub(".jpg", "", fts)
    fts = ymd_hms(fts)
    inRange = which(fts >= RANGE[1] & fts <= RANGE[2])
    ff = ff[inRange]
    fts = fts[inRange]
    ## loop over files within range
    for (i in seq(along=ff)) {
        f = ff[i]
        ts = fts[i]
        if (ts < minTS)
            next
        ## we'll use this one; bump up next desired ts
        minTS = ts + STEP
        destf = file.path(DESTFOLDER, basename(f))
        okay = FALSE
        try({
            x = readJPEG(f)[,,1]
            xx = readJPEG(f, native=TRUE)
            dim(xx)=rev(dim(xx))
            okay = TRUE
        })
        if (! okay)
            next
        trans = which(t(xx) == 0)
        x[trans] = 0
        jpeg(destf, ncol(x), nrow(x), type="cairo", antialias="gray")
##        X11(":4", 8, 8, type="cairo")

        ## set parameters so that the image will occupy the entire plotting region
        par(fig=c(0,1,0,1),omd=c(0, 1, 0, 1),oma=c(0,0,0,0), mai=c(0,0,0,0), plt=c(0,1,0,1), usr=c(0,1,0,1))

        plot(c(0, ncol(x)), c(0, nrow(x)), type="n", xaxt="n", yaxt="n", xaxs="i", yaxs="i")
        rasterImage(x, 0, 0, ncol(x), nrow(x))

        ## add a text timestamp label
##        dts = list(x=15, y=20)

##        dts = list(x=781, y=1005)
        dts = list(x = ncol(x) * 0.25, y = nrow(x) * 0.97)

        cfact = 3

        text(dts$x, dts$y, format(TS(ts), "%Y %b %d (%a) %H:%M:%OS1 GMT"), col="white", pos=4, font=2, family="mono", cex=1.6 * cfact)

        ## add a scale bar
        ## sb = list(x=569, y=750)
        sb = list(x=ncol(x) * 0.5, y = nrow(x) * 0.93)
##        scale = 9 ## metres per pixel
        scale = 4.8 ## metres per pixel (as of 2016 Nov 6)
        lines(c(sb$x, sb$x+1000/scale), c(sb$y, sb$y), lty=1, lwd=3, col="white")
        text(sb$x + 1000 / scale / 2, sb$y-40, "1 km", font = 2, family="mono", cex=2*cfact, col="white")

        ## add a tide gauge; gx, gy is the lower left corner
#        gx = 20
#        gy = 50
        ## gx = 360
        ## gy = 18

        gx = ncol(x) * 0.78
        gy = nrow(x) * 0.72

        ## scaling is 44 pixels width per 6 hours, 23 pixels height per 5 metres
        gsx = 2 * 44 / (6 * 3600)
        gsy = 4 * 23 / 5

        ## draw axes: -12h ... 12h @ 0m, -5m .. +15m @ 0h
        segments(c(gx, gx + gsx * 12 * 3600), c(gy + gsy * 5, gy), c(gx + gsx * 24 * 3600, gx + gsx * 12 * 3600), c(gy + gsy * 5, gy + gsy * 20),
                 col="white", lwd=2)
        ## draw ticks
        segments(c(gx + gsx * 24 * 3600, gx + gsx * 12 * 3600-10), c(gy + gsy * 5 - 10, gy + gsy * 20), c(gx + gsx * 24 * 3600, gx + gsx * 12 * 3600+10), c(gy + gsy * 5+ 10, gy + gsy * 20),
                 col="white", lwd=2)

        ## axis scales:
##        text(c(gx + gsx * 12 * 3600, gx + gsx * 24 * 3600), c(gy + gsy * 20, gy + gsy * 5), c("15m", "12h"), pos=c(3, 4), cex=1.5, font=2, family="mono", col="white")
        text(c(gx + gsx * 18 * 3600, gx + gsx * 19 * 3600), c(gy + gsy * 18, gy + gsy * 2), c("15m", "12h"), pos=c(3, 4), cex=1.5 * cfact, font=2, family="mono", col="white")

        ## times for approximating tide
        tt = seq(from = ts - 12 * 3600, to = ts + 12 * 3600, length = 1 + 24 * 3600 * gsx)

        ## tide levels at those times
        th = tide(tt)

        ## plot the tide curve from -12h to 12h around current sweep
        lines(gx:(gx + gsx * 24 * 3600), gy + gsy * (5+th), lwd=2*cfact, col="white")

        ## add current point
        points(gx + gsx * 12 * 3600, gy + gsy * (5 + th[ceiling(length(th) / 2)]), col="white", bg="white", pch=19, cex=2*cfact)

        dev.off()

        ## output filename
        cat(destf, "\n")
    }
    day = day + 24 * 3600
}
