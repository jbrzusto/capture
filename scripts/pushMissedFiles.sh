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
    YEAR=${f:0:4}
    MONTH=${f:4:2}
    DAY=${f:6:2}
    DATE=$YEAR-$MONTH-$DAY
    ssh radar2@force2 mkdir /volume1/all/radar/fvc/pol/$DATE
    if ( scp -l 10000 -P 30022 $f radar2@force2:/volume1/all/radar/fvc/pol/$DATE ); then
        rm -f $f
        if [[ "$f{/*./}" == "bz2" ]]; then
            sleep 20
        fi
    fi
done
