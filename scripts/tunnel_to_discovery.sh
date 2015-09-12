#!/bin/bash

# try to maintain an open ssh tunnel to discovery.acadiau.ca

# this script can safely be run every 10 minutes from a crontab
# (we've white-listed the apparent IP address at fundy force
# with discovery's sshdguard).

# first, check whether there is already a tunnel open, by logging
# in remotely to discovery, then tunneling back to grab the hostname

resp=`ssh force-radar@discovery ssh -p 30022 radar@localhost cat /hostname.txt`

# if the tunnel is working, then $resp should be `cat /hostname.txt`:

if [[ "$resp" == "`cat /hostname.txt`" ]]; then
    exit 0
fi

# tunnel not working, so kill off any ssh processes to avoid zombies

old=`psg ssh | grep :30022: | awk '{print $1}'`
kill -KILL $old

# tunnel port 30022 on discovery back to this hosts's ssh port

ssh -N -f -R localhost:30022:localhost:22 force-radar@discovery
