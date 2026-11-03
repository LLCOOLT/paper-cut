#pragma once
/* PC port: Aurora ships no copy of this SDK header; reuse the decomp's own (dolsdk2004).
 * The headless backend finds the original directly. */
#if defined(TTYD_BACKEND_AURORA)
#include "../../../libs/dolsdk2004/include/demo/DEMOPad.h"
#else
#include_next <demo/DEMOPad.h>
#endif
