#!/bin/bash
# start the capture, filing, and export of radar sweeps
cd ~/capture
date >> capturelog.txt
export DATADIR=/radar_sweeps
scripts/capture -d 2 -n 3744 -p 7500 -c 1 -r 0.43:0.12 >> capturelog.txt 2>&1
