#!/home/johnb/bin/Rscript
##
## move radar files from the spool directory to ongoing storage

## Directory receiving radar files:
## NB: *must* include trailing '/'
RADAR_SPOOL = "/mnt/raid1/radar/fvc/"

## directory where ongoing radar storage is mounted
## each disk is mounted in a subfolder, then sweeps are stored
## in subfolders two levels down with paths %Y-%m-%d/%H
OUTPUT_PATH_TEMPLATE = "/mnt/raid1/radar/fvc/%Y/%m-%d/"

## start the inotifywait command, which will report events in the radar spool directory.
## we're interested in these:
## - close of file in spool dir after it has been read by the process which scan converts it to a JPEG
## and pushes that to a different server; e.g.:
##  /radar_spool/,FORCEVC-2015-11-04T03-02-54.169557.dat,CLOSE_NOWRITE,CLOSE
##
## FIXME: watch storage directory for addition of new mounts

evtCon = pipe(paste("/usr/bin/inotifywait -q -m -e close_nowrite --format %w,%f,%e", RADAR_SPOOL), "r")

## get list of files already in spool directory

spoolFiles = dir(RADAR_SPOOL)

while (TRUE) {
    if (length(spoolFiles) > 0L) {
        evt = matrix(c(RADAR_SPOOL, spoolFiles[1], "CLOSE_NOWRITE"), nrow=1)
        spoolFiles = spoolFiles[-1]
    } else {
        evt = readLines(evtCon, n=1)
        evt = read.csv(textConnection(evt), as.is=TRUE, header=FALSE)
    }
    fn = evt[1, 2]
    
    if (evt[1,1] == RADAR_SPOOL     &&
        evt[1,3] == "CLOSE_NOWRITE" &&
        ! is.na(fn)                 &&
        ) {
        ## new file, so move it to the appropriate location
        ## all files are supposed to start with a timestamp in YYYYMMDDHHMMSS format

        ts = ymd_hms(substr(fn, 1, 14))

        dest = file.path(format(ts, OUTPUT_PATH_TEMPLATE), fn)

        dir.create(dirname(dest), recursive=TRUE)

        file.rename(fn, dest)
    }
}

        
               
