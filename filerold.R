#!/usr/bin/Rscript
##
## move *existing* radar files from the spool directory to ongoing
## storage. Multiple drives are assumed to be mounted in a common
## directory, and as storage is filled, the oldest files are deleted,
## day by day.

## directory where radar sweeps are written by the capture program
## NB: *must* include trailing '/'
ARGV = commandArgs(TRUE)

if (length(ARGV) > 0) {
    RADAR_SPOOL = ARGV[1]
} else {
    RADAR_SPOOL = "/radar_sweeps/"
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

days = data.frame(path = I(days), date = basename(days))
## sort the day folders from oldest to newest

days = days[order(days$date),]

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

## only check free space after every 500 files

fsCheckAt = 500L
fsCheckCounter = fsCheckAt

## main loop; it checks for new files, archives an old file (if there
## are any), sometimes checks for free space on destination storage
## If there's nothing to do, it sleeps 100ms

while (length(spoolFiles) > 0L) {
    tryCatch({
        if (fsCheckCounter == fsCheckAt) {
            free = getFreeSpace()
            while (sum(free) < FREE_THRESH) {
                ## delete files from oldest day
                system(paste("rm -rf", days$path[1]))
                days = days[-1,]
                free = getFreeSpace()
            }
            fsCheckCounter = 0L
            curDrive = drives[which.max(free)]
        }
        fn = spoolFiles[1]
        spoolFiles = spoolFiles[-1]
        from = file.path(RADAR_SPOOL, fn)

        ## move file to the appropriate location we are
        ## guaranteed by preceding code to have space for it

        parts = strsplit(fn, "[-T]", perl=TRUE)[[1]]

        ## parts looks like:
        ##    [1] "FORCEVC"       "2015"          "11"            "04"
        ##    [5] "03"            "02"            "54.169557.dat"

        date = sprintf("%s-%s-%s", parts[2], parts[3], parts[4])

        path = file.path(curDrive, date)

        if (! path %in% days$path) {
            dir.create(path, recursive=TRUE)
            days = rbind(days, data.frame(path = I(path), date=I(date)))
        }

        ## compress the file to a new location in storage
        cmd = sprintf("gzip -c '%s' > '%s.gz'; /bin/rm -f '%s'",
                      from,
                      file.path(path, fn),
                      from)
        system(cmd, wait=FALSE)
        cat(fn, "\n")
        fsCheckCounter = fsCheckCounter + 1L
    }, error=function(e) {
        cat(as.character(e), "\n")
        Sys.sleep(1)
    })
}
