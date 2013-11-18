##
## force.R - work with data being live-captured into an SQLite database
##

library(RSQLite)
library(lubridate)

## shorthand query function

sql = function(con, ...) {
  dbGetQuery(con, sprintf(...))
}

connect = function(DATA_DIR = "/mnt/3tb/force_data", ts=as.numeric(Sys.time())) {
  ## return a connection to the database containing data for ts (and presumably later)
  ## data are saved in files called forceYYYY-MM-DDTHH-MM-SS.NNN.sqlite
  ## where YYYY-MM-DD is the date, HH-MM-SS.NNN is the start time, in GMT

  ## get list of all files in the data directory
  files = dir(DATA_DIR, pattern="^force2[0-9]{3}.*.sqlite$", full.names=TRUE)

  ## parse starting timestamps from filenames (dropping 'force' prefix)
  filets = ymd_hms(substring(basename(files), 6))

  ## use latest file whose starting timestamp is at most specified one
  ## (files are sorted in alphabetical order, hence by timestamp)
  file = tail(files[filets <= ts], 1)

  if (length(file) == 0)
    stop("No file appears to have data for the specified timestamp")
  ## it's an SQLite database, return a connection to it
  dbConnect("SQLite", file)
}

getSweep = function(con, ts = as.numeric(Sys.time())) {
  ## return the raw pulse data from the sweep containing ts

  ## find which sweep that is
  sweep = sql(con, "select sweep_key from pulses where ts >= %.3f order by ts limit 1", ts)[1,1]

  if (is.na(sweep))
    stop("No sweep with specified timestamp")

  sql(con, "select * from pulses where sweep_key=%d order by ts", sweep)
}

asMatrix = function(sweep) {
  ## return the sweep data as a matrix of integers, with each column
  ## corresponding to data from a single pulse
  c = nrow(sweep)
  r = length(sweep$samples[[1]]) / 2
  h = readBin(unlist(sweep$samples), integer(), size=2, n=r * c)
  dim(h) <- c(r, c)
  return(h)
}
  

showSweep = function(sweep) {
  ## plot the raw sweep data
  title = strftime(structure(sweep$ts[1], class="POSIXct"), "Fundy FORCE visitor centre radar sweep @ %Y-%m-%d %H:%M:%OS3")
  m = asMatrix(sweep)
##  image(z=m, main=title, xlab="Azimuth (degrees)", ylab="Range (km)", useRaster=TRUE, x=(0:(ncol(m)-1)) / ncol(m) * 360, y = (0:(nrow(m)-1)) / nrow(m) * 2.34375)
  image(z=m, main=title, ylab="Azimuth (degrees)", xlab="Range (km)", useRaster=TRUE)
}

latest = function(con) {
  x = getSweep(con, as.numeric(Sys.time()) - 2.2)
  showSweep(x)
  return(invisible(x))
}
