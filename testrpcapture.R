## test the rpcapture code by pushing data from an already-recorded
## pulse database to it in the appropriate wire format


## start up rpcapture

system("rm -f /tmp/test.sqlite*; ./rpcapture -P 12345 -n 2592 -p 15000 /tmp/test.sqlite",
       wait = FALSE)

## give rpcapture a second to bring up its listening port
Sys.sleep(1)

## connect to the running rpcapture instance via port 12345

fout = socketConnection("localhost", 12345, blocking=TRUE, open="wb", timeout=0)

library(RSQLite)

## connect to the database of 12287 pulses
con = dbConnect("SQLite", "/home/data/force-radar/force_2015_01_29T02_00_00.sqlite")

x = dbGetQuery(con, "select * from pulses order by ts")

## magic codes for start of pulse, and end of all data
PULSE_METADATA_MAGIC = as.raw(as.integer(
    c(0xba, 0xdc, 0xcd, 0xab, 0x0f, 0xf0, 0x0f, 0xf0)))

PULSE_METADATA_DONE_MAGIC = as.raw(as.integer(
    c(0xf0, 0x0f, 0xf0, 0x0f, 0x0f, 0xf0, 0x0f, 0xf0)))

## dump the data from the uplse database
last_sweep_key = -1
arp_clock = 0

for (i in 1:nrow(x)) {
    writeBin(PULSE_METADATA_MAGIC, fout)
    if (last_sweep_key != x$sweep_key[i]) {
        last_sweep_key = x$sweep_key[i]
        arp_clock = x$ts[i]
    }
    writeBin(as.integer(floor(arp_clock)), fout, size=4)
    writeBin(as.integer(1e9 * (arp_clock - floor(arp_clock))), fout, size=4)
    writeBin(x$trig_clock[i], fout, size=4)
    writeBin(x$azi[i], fout, size=4)
    writeBin(x$trigs[i], fout, size=4)
    writeBin(x$sweep_key[i], fout, size=4)
    writeBin(x$samples[[i]], fout)
}

## write end-of-data magic number and (more than) enough zeros
## to fill a pulse buffer

writeBin(c(PULSE_METADATA_DONE_MAGIC, raw(16384)), fout)
close(fout)

