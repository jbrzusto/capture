#!/bin/bash
## launch rpcapture, and restart if it fails
LOGFILE=/tmp/rpcapturelog.txt

while (( 1 )); do
    /home/radar/capture/rpcapture "$@" >> $LOGFILE
    sleep 30
    date >> $LOGFILE
    echo "Restarting rpcapture" >> $LOGFILE
done
