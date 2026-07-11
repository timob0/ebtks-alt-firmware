# Modifications after cloning the source tree #

July 1 2026
- Deleted the Base64.h from include due to a compile error indicating the .progmem segment declaration conflicting with the arduino-base library
-- Added Base64 to the lib-deps section in env: in the platformio.ini file
- Removed calls to begin/end/final Base64.streamEncode that are unsupported by the Base64 library and apparently have been patched in by the previous authors
-- affected: EBTKS_CRT.cpp, needs to be resolved at some point but low priority, can't think of a usecase for this
- Wrapped some memory arithmetic in EBTKS_Utilities.cpp in a pragma statement to silence an array out of bounds Warning
- Replaced the __dis/__enableIrq() calls in the three files as per the LibraryPatches/Readme.md; did not replace files but lines in source due to version drifts.
-> Builds cleanly.
