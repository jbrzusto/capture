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

## start the inotifywait command, which will report events in the radar spool directory.
## we're interested in these:
## - close of file in spool dir after it has been read by the process which scan converts it to a JPEG
## and pushes that to a different server; e.g.:
##  /radar_spool/,FORCEVC-2015-11-04T03-02-54.169557.dat,CLOSE_NOWRITE,CLOSE
##
## FIXME: watch storage directory for addition of new mounts

evtCon = NULL

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
    if (length(spoolFiles) > 0) {
        evt = matrix(c(RADAR_SPOOL, spoolFiles[1], "CLOSE_NOWRITE"), nrow=1)
        spoolFiles = spoolFiles[-1]
    } else {
        if (is.null(evtCon)) {
            evtCon = pipe(paste("/usr/bin/inotifywait -q -m -e close_nowrite --format %w,%f,%e", RADAR_SPOOL), "r")
        }
        evt = readLines(evtCon, n=1)
        evt = read.csv(textConnection(evt), as.is=TRUE, header=FALSE)
    }
    if (evt[1,1] == RADAR_SPOOL && evt[1,3] == "CLOSE_NOWRITE") {
        ## new file, so move it to the appropriate location
        ## we are guaranteed by preceding code to have space
        ## for it
        parts = strsplit(evt[1,2], "[-T]", perl=TRUE)[[1]]

        ## parts looks like:
        ##    [1] "FORCEVC"       "2015"          "11"            "04"           
        ##    [5] "03"            "02"            "54.169557.dat"
        
        dateHour = sprintf("%s-%s-%s/%s", parts[2], parts[3], parts[4], parts[5])

        path = file.path(curDrive, dateHour)

        if (! path %in% hours$path) {
            dir.create(path, recursive=TRUE)
            hours = rbind(hours, data.frame(path = I(path), dateHour=I(dateHour)))
        }

        from = file.path(RADAR_SPOOL, evt[1,2])
        ## compress the file to a new location in storage
        cmd = sprintf("gzip -c '%s' > '%s.gz'",
                       from,
                       file.path(path, evt[1,2]))
        ## cat("About to do ", cmd, "\n")
        system(cmd, wait=TRUE)

        file.remove(from)
        fsCheckCounter = fsCheckCounter + 1L
    }
}

        
               
