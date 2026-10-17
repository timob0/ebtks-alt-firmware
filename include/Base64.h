#pragma once

// Compatibility wrapper: use the PlatformIO-provided Base64 library.
// Kept as a local header only so existing #include "Base64.h" sites remain unchanged.
#include_next <Base64.h>
