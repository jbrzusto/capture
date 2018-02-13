#!/usr/bin/Rscript
##
## move radar files from the spool directory to ongoing storage
## Multiple drives are assumed to be mounted in a common directory,
## and as storage is filled, the oldest files are deleted, hour by hour.

## directory where radar sweeps are written by the capture program
## NB: *must* include trailing '/'
ARGV = commandArgs(TRUE)

if (length(ARGV) > 0) {
    RADAR_SPOOL = ARGV[1]
} else {
    RADAR_SPOOL = "/radar_spool/"
}

## directory where ongoing radar storage is mounted
## each disk is mounted in a subfolder, then sweeps are stored
## in subfolders two levels down with paths %Y-%m-%d/%H
if (length(ARGV) > 1) {
    RADAR_STORE = ARGV[2]
} else {
    RADAR_STORE = "/mnt/radar_storage"
}

## regex matching sweep files
SWEEP_FILE_REGEX = "\\.dat$"

## threshold for free space (bytes) in total radar storage.
## when free space drops below this value, a delete
## of the oldest hour(s) of files occurs until the free space
## rises above this threshold.
## 12 GB should cover 1 hour.
FREE_THRESH = 12e9

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

## each file moved to the RADAR_SPOOL folder generates an event, which inotifywait
## writes to a fifo.  We open that fifo with blocking=FALSE so we can check for
## new files while also archiving existing files

FIFO = paste0("/tmp/new_sweep_", basename(RADAR_SPOOL))

system("mkfifo /tmp/new_sweep")
system(paste("/usr/bin/inotifywait -q -m -e moved_to --format %w,%f,%e", RADAR_SPOOL, ">", FIFO), wait=FALSE)

evtCon = fifo(FIFO, "r", blocking=FALSE)

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

## main loop; it checks for new files, archives an old file (if there
## are any), sometimes checks for free space on destination storage
## If there's nothing to do, it sleeps 100ms

while (TRUE) {
    tryCatch({
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
        evt = NULL
        ## see whether there's a new file in the FIFO
        a = readLines(evtCon, 1)
        if (length(a) > 0) {
            e = strsplit(a, ",")[[1]]
            if (! is.na(e[2]) && ! isTRUE(e[5]=="ISDIR"))
                evt = e
        }
        ## if no new file, check for an existing file
        if (is.null(evt)) {
            if (length(spoolFiles) > 0L) {
                evt = c(RADAR_SPOOL, spoolFiles[1], "EXISTING")
                spoolFiles = spoolFiles[-1]
            }
        }
        if (is.null(evt)) {
            ## still nothing, sleep a bit then retry
            Sys.sleep(0.1)
            next
        }
        fn = evt[2]
        from = file.path(RADAR_SPOOL, fn)

        if (evt[1] == RADAR_SPOOL) {
            if (isTRUE(evt[3] == "EXISTING" || evt[3] == "MOVED_TO")) {
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
                cat(fn, "\n")
                fsCheckCounter = fsCheckCounter + 1L
            }
        }
    }, error=function(e) {
        cat(as.character(e), "\n")
        Sys.sleep(1)
    })
}
