#!/bin/sh
# copy all *fvc.pol.bz2 and *fvc.jpg files from /tmp to
# the FORCE HQ server, deleting the local copy when successful
for x in /tmp/*fvc.jpg /tmp/*fvc.pol.bz2; do
   if ( scp -oControlMaster=no -oControlPath=none -i ~/.ssh/id_dsa_vc_radar_laptop $x radar_upload@force:data ) then
      rm -f $x
   fi
done
