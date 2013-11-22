#!/usr/bin/Rscript
#
# This generates the file radarImagePalette.rds, whose contents are an integer vector of length 256.
# Each integer represents palette as 0xAABBGGRR, where AA is alpha, and the rest are the red, green,
# and blue components.
#
# Samples values are mapped to colour values like so:
# i = sample / 16
# (i is the zero-based index into the palette).  This converts a 12 bit sample value into
# an 8 bit palette value.  For now, no dithering.
#
# Because of the current DC bias in the video signal, samples values don't go much below 2048, so
# we make the first chunk of the palette be transparent, for a cleaner overlay image.
# Then we ramp through colours and increasing opacity.

# The easiest way to generate the colour values is to use the palette editor in radR, then
# do from the radR console
#
#  cat(deparse(RSS$palette.mat[, 3]), "\n)
#
# where '3' will need to be chosen to find the particular palette you've edited.

pal = c(0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,  0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,  0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,  0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,  0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,  0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,  0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,  0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,  0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 135135232L,  439156736L, 759955456L, 1063976960L, 1367998464L, 1688862720L,  1992884224L, -1998061568L, -1744894720L, -1677780224L, -1610665984L,  -1543551744L, -1459660288L, -1392546048L, -1325431808L, -1258317568L,  -1191203328L, -1124089088L, -1056974848L, -973083136L, -922878208L,  -873267456L, -840433920L, -790823168L, -757989632L, -725221632L,  -675610880L, -642777344L, -593166592L, -560333056L, -527499520L,  -477888768L, -445055232L, -395444480L, -362676480L, -329842944L,  -280232192L, -247398656L, -197787904L, -164954368L, -132120832L,  -82510080L, -49742080L, -16712189L, -16712946L, -16713702L, -16714459L,  -16715215L, -16715972L, -16716728L, -16717485L, -16718241L, -16718998L,  -16719754L, -16720767L, -16721523L, -16722280L, -16723036L, -16723793L,  -16724549L, -16725306L, -16726062L, -16726819L, -16727575L, -16728331L,  -16729345L, -16731137L, -16733185L, -16735233L, -16737281L, -16739073L,  -16741121L, -16743169L, -16745217L, -16747009L, -16749057L, -16751105L,  -16753153L, -16754945L, -16756993L, -16759041L, -16761089L, -16762881L,  -16764929L, -16766977L, -16768769L, -16770817L, -16772865L, -16774913L,  -16776705L, -100007681L, -183238401L, -266403585L, -349634305L,  -449576705L, -532807425L, -616038145L, -699203329L, -799211265L,  -882376449L, -965607169L, -1048772353L, -1132003073L, -1231945473L,  -1315176193L, -1398341377L, -1481572097L, -1564737281L, -1664745217L,  -1747910401L, -1831141121L, -1914371841L, -2014314241L, -2097544961L )

saveRDS(pal, "/home/radar/capture/radarImagePalette.rds")
