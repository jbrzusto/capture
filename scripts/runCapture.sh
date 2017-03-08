#!/bin/bash
# start the capture, filing, and export of radar sweeps
cd ~/capture
date >> capturelog.txt
export DATADIR=/radar_spool
scripts/capture -d 2 -n 2496 -p 7500 -c 1 -r 0.43:0.12 >> capturelog.txt 2>&1
