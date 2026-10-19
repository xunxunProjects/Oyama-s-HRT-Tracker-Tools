#ifndef HRTCORE_H
#define HRTCORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HRTCORE_ABI_VERSION 1u

/* Version of the C core implementation. */
const char *hrtcore_version(void);

/* ABI revision of this header. */
uint32_t hrtcore_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif /* HRTCORE_H */
