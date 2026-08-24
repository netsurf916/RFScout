# microSD card compatibility

RFScout uses the microSD/TF-card interface provided by the LILYGO T-Embed CC1101 Plus.

## LILYGO compatibility guidance

LILYGO's documentation for the T-Embed CC1101 family notes that its tested microSD configuration is **SanDisk cards with a capacity of 32 GB or less**. LILYGO also notes that some other cards may not work reliably and that the reason for those compatibility differences is not known.

For RFScout, a **32 GB or smaller FAT32-formatted SanDisk microSD card** should therefore be treated as the known-good baseline. Larger cards or cards from other manufacturers may work, but they are outside LILYGO's stated tested configuration and should not be assumed compatible.

In particular, cards larger than 32 GB are commonly supplied formatted as exFAT rather than FAT32. RFScout currently uses ESP-IDF's FAT filesystem SD-SPI mounting path, so filesystem format is another possible source of incompatibility independent of the physical card capacity.

## Detection behavior

The T-Embed CC1101 Plus does not expose a separate microSD card-detect GPIO in the board pin mapping used by RFScout. RFScout therefore considers storage present only after the card successfully initializes and mounts. An installed but incompatible or unmountable card is treated the same as unavailable storage.

Failure to mount an SD card is nonfatal. RFScout continues operating without logging, and the SD status icon is shown only after storage has mounted successfully.

## RFScout logging footprint

The current diagnostic logger uses up to 100 rotating files of 4 MiB each (`rfscout-00.log` through `rfscout-99.log`), for an approximate maximum log footprint of 400 MiB. RFScout also attempts to preserve at least 64 MiB of free filesystem space.
