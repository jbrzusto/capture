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
## - move of a file to the spool dir.
##
## FIXME: watch storage directory for addition of new mounts

if (! oldOnly) {
    evtCon = pipe(paste("/usr/bin/inotifywait -q -m -e moved_to --format %w,%f,%e", RADAR_SPOOL), "r")
} else {
    evtCon = NULL
}

## get list of files already in spool directory

spoolFiles = sort(dir(RADAR_SPOOL), decreasing=TRUE)

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

## allow up to 2 existing spool file moves for each new one added
existingFileMoveMax = if (newOnly) 0 else 2L
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
        evt = c(RADAR_SPOOL, spoolFiles[1], "EXISTING")
        spoolFiles = spoolFiles[-1]
        existingFileMoveCount = existingFileMoveCount - 1L
    } else if (! oldOnly) {
        repeat {
            evt = strsplit(readLines(evtCon, n=1), ",")[[1]]
            if (! is.na(evt[2]) && ! isTRUE(evt[5]=="ISDIR"))
                break
            Sys.sleep(0.1)
        }
    }
    fn = evt[2]
    if (is.na(fn))
        next
    from = file.path(RADAR_SPOOL, fn)

    if (isTRUE(file.info(from)$isdir))
        next

    if (evt[1] == RADAR_SPOOL) {
        if (evt[3] == "EXISTING" || evt[3] == "MOVED_TO") {
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

            ## compress the file to a new location in storage
            cmd = sprintf("gzip -c '%s' > '%s.gz'; /bin/rm -f '%s'",
                          from,
                          file.path(path, fn),
                          from)
            system(cmd, wait=FALSE)

            fsCheckCounter = fsCheckCounter + 1L
            if (evt[3] != "EXISTING") {
	        existingFileMoveCount = existingFileMoveMax
	    }
        }
    }
}
