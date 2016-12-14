#!/bin/bash

# Push image and compressed .pol files left by missed connections.
#
# When the internet connection to the FORCE HQ box is dropped, the
# periodic export.R task leaves the generated files in /tmp.
#
# This script pushes any such files to FORCE HQ, and deletes them
# from /tmp
#
# We run this script nightly

cd /tmp

for f in `ls -1 *bz2 2*jpg | sort -r` ; do
    if ( scp -oControlMaster=no -oControlPath=none -i ~/.ssh/id_dsa_vc_radar_laptop $f radar_upload@force:data ); then
        rm -f $f
        sleep 20  ## be nice about it
    fi
done
