#ifndef HRTPRINT_H
#define HRTPRINT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the Rust printing library. */
const char *hrtprint_version(void);

/*
 * Render a JSON view model. `format` is "table" or "json". Returns 0 on
 * success, 1 for invalid JSON, 2 for a too-small output buffer, 3 for an
 * unknown format, 4 for oversized input, and 5 for null pointers.
 */
int hrtprint_render(const char *json, const char *format, char *out, size_t out_cap,
                    size_t *out_len, char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* HRTPRINT_H */
