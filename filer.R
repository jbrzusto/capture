#!/usr/bin/Rscript
##
## move radar files from the spool directory to ongoing storage
## Multiple drives are assumed to be mounted in a common directory,
## and as storage is filled, the oldest files are deleted, hour by hour.

## directory where radar sweeps are written by the capture program
## NB: *must* include trailing '/'
RADAR_SPOOL = "/radar_spool/"

## directory where ongoing radar storage is mounted
## each disk is mounted in a subfolder, then sweeps are stored
## in subfolders two levels down with paths %Y-%m-%d/%H
RADAR_STORE = "/mnt/radar_storage"

## regex matching sweep files
SWEEP_FILE_REGEX = "\\.dat$"

## threshold for free space (bytes) in total radar storage.
## when free space drops below this value, a delete
## of the oldest hour(s) of files occurs until the free space
## rises above this threshold.
## 12 GB should cover 1 hour.
FREE_THRESH = 12e9  

## if user specifes --old, we only archive existing files
## from spool folder.  If user specifies --new, we only archive newly-arrived
## files from the spool folder

ARGV = commandArgs(TRUE)
oldOnly = FALSE
newOnly = FALSE

while(length(ARGV) > 0) {
    if (ARGV[1] == "--old") {
        oldOnly = TRUE
        ARGV = ARGV[-1]
    } else if (ARGV[1] == "--new") {
        newOnly = TRUE
        ARGV = ARGV[-1]
    }
}

if (oldOnly && newOnly) {
    stop("You must specify at most one of --old and --new.")
}

## get the drives used for storage
drives = dir(RADAR_STORE, full.names=TRUE, pattern="^sd.*$")

## get the list of day folders
days = dir(drives, full.names=TRUE, pattern="^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")

## get the list of hour folders
hours = dir(days, full.names=TRUE, pattern="^[0-9][0-9]$")

hours = data.frame(path = I(hours), dateHour = I(file.path(basename(dirname(hours)), basename(hours))))
## sort the hour folders from oldest to newest

hours = hours[order(hours$dateHour),]

## start the inotifywait command, which will report events in the radar spool directory.
## we're interested in these:
## - creation of a file
## - close of that file in spool dir after it has been read by the process which scan converts it to a JPEG
## and pushes that to a different server; e.g.:
##  /radar_spool/,FORCEVC-2015-11-04T03-02-54.169557.dat,CLOSE_NOWRITE,CLOSE
##
## FIXME: watch storage directory for addition of new mounts

if (! oldOnly) {
    evtCon = pipe(paste("/usr/bin/inotifywait -q -m -e close_nowrite -e create --format %w,%f,%e", RADAR_SPOOL), "r")
} else {
    evtCon = NULL
}

## get list of files already in spool directory

spoolFiles = dir(RADAR_SPOOL)

getFreeSpace = function() {
    ## return number of bytes free on disks mounted under radar storage folder

    free = unlist(
        lapply(
            drives, 
            function(d) {
                v = read.table(textConnection(system(paste("df", d), intern=TRUE)[2]), as.is=TRUE)
                if (sub("[0-9]","", basename(v[1,1])) == basename(d)) {
                    return (v[1,4] * 1024)
                } else {
                    return(0)
                }
            }
        )
    )
    names(free) = drives
    return(free)
}

## only check free space after every 100 files

fsCheckAt = 100L
fsCheckCounter = fsCheckAt

## keep track of most recently created file, which will be the one
## pushed, once it has been closed on a read.
lastCreated = ""

## allow up to 1 existing spool file moves for each new one added
existingFileMoveMax = if (newOnly) 0 else 5L
existingFileMoveCount = existingFileMoveMax

while (TRUE) {
    if (fsCheckCounter == fsCheckAt) {
        free = getFreeSpace()
        while (sum(free) < FREE_THRESH) {
            ## delete files from oldest hour
            system(paste("rm -rf", hours$path[1]))
            hours = hours[-1,]
            free = getFreeSpace()
        }
        fsCheckCounter = 0L
        curDrive = drives[which.max(free)]
    }
    if (length(spoolFiles) > 0L && (oldOnly || existingFileMoveCount > 0L)) {
        evt = matrix(c(RADAR_SPOOL, spoolFiles[1], "EXISTING"), nrow=1)
        spoolFiles = spoolFiles[-1]
        existingFileMoveCount = existingFileMoveCount - 1L
    } else if (! oldOnly) {
        evt = readLines(evtCon, n=1)
        evt = read.csv(textConnection(evt), as.is=TRUE, header=FALSE)
        existingFileMoveCount = existingFileMoveMax
    }
    ## print(evt)
    fn = evt[1, 2]
    
    if (evt[1,1] == RADAR_SPOOL) {
        if (evt[1,3] == "CREATE" &&
            grepl( SWEEP_FILE_REGEX, fn, perl=TRUE)
            ) {
            lastCreated = fn
        } else if (evt[1,3] == "EXISTING" || ( "CLOSE_NOWRITE" &&
                   ! is.na(fn) &&
                   fn == lastCreated)) {
            ## new file, so move it to the appropriate location
            ## we are guaranteed by preceding code to have space
            ## for it

            parts = strsplit(fn, "[-T]", perl=TRUE)[[1]]

            ## parts looks like:
            ##    [1] "FORCEVC"       "2015"          "11"            "04"           
            ##    [5] "03"            "02"            "54.169557.dat"
            
            dateHour = sprintf("%s-%s-%s/%s", parts[2], parts[3], parts[4], parts[5])

            path = file.path(curDrive, dateHour)

            if (! path %in% hours$path) {
                dir.create(path, recursive=TRUE)
                hours = rbind(hours, data.frame(path = I(path), dateHour=I(dateHour)))
            }

            from = file.path(RADAR_SPOOL, fn)
            ## compress the file to a new location in storage
            cmd = sprintf("gzip -c '%s' > '%s.gz'; rm -f '%s'",
                          from,
                          file.path(path, fn),
                          from)
            ## cat("About to do ", cmd, "\n")
            system(cmd, wait=FALSE)

            fsCheckCounter = fsCheckCounter + 1L
        }
    }
}

        
               
