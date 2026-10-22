#include "hrtcore/hrtcore.h"

const char *hrtcore_version(void) {
#ifdef HRT_CORE_VERSION_STRING
    return HRT_CORE_VERSION_STRING;
#else
    return "0.1.1";
#endif
}

uint32_t hrtcore_abi_version(void) {
    return HRTCORE_ABI_VERSION;
}
