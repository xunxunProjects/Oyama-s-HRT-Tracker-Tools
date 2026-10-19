#include "hrtcore/hrtcore.h"

#include <limits.h>
#include <openssl/evp.h>
#include <stdbool.h>
#include <stdio.h>

static void set_error(hrtcore_error *err, int code, const char *message) {
    if (err == NULL) {
        return;
    }
    err->code = code;
    if (message == NULL) {
        err->message[0] = '\0';
        return;
    }
    snprintf(err->message, sizeof(err->message), "%s", message);
}

static void clear_error(hrtcore_error *err) {
    if (err != NULL) {
        err->code = HRTCORE_OK;
        err->message[0] = '\0';
    }
}

int hrtcore_b64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap,
                       size_t *out_len, hrtcore_error *err) {
    clear_error(err);
    if (out_len == NULL || out == NULL || (in == NULL && in_len != 0)) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid base64 encode argument");
        return HRTCORE_ERR_INVALID_ARG;
    }
    if (in_len > (size_t)INT_MAX) {
        set_error(err, HRTCORE_ERR_LIMIT, "base64 input too large");
        return HRTCORE_ERR_LIMIT;
    }

    const size_t needed = 4 * ((in_len + 2) / 3);
    if (out_cap < needed + 1) {
        set_error(err, HRTCORE_ERR_BUFFER_TOO_SMALL, "base64 output buffer too small");
        return HRTCORE_ERR_BUFFER_TOO_SMALL;
    }

    const int written = EVP_EncodeBlock((unsigned char *)out, in, (int)in_len);
    if (written < 0 || (size_t)written != needed) {
        set_error(err, HRTCORE_ERR_CRYPTO, "base64 encode failed");
        return HRTCORE_ERR_CRYPTO;
    }
    out[needed] = '\0';
    *out_len = needed;
    return HRTCORE_OK;
}

int hrtcore_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap,
                       size_t *out_len, hrtcore_error *err) {
    clear_error(err);
    if (out_len == NULL || in == NULL || (out == NULL && out_cap != 0)) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid base64 decode argument");
        return HRTCORE_ERR_INVALID_ARG;
    }
    if (in_len == 0) {
        *out_len = 0;
        return HRTCORE_OK;
    }
    if (in_len % 4 != 0 || in_len > (size_t)INT_MAX) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "base64 input length is not a multiple of 4");
        return HRTCORE_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < in_len; ++i) {
        const char c = in[i];
        const bool alphabet = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9') || c == '+' || c == '/';
        if (c == '=') {
            if (i + 2 < in_len) {
                set_error(err, HRTCORE_ERR_INVALID_ARG, "base64 padding before the end");
                return HRTCORE_ERR_INVALID_ARG;
            }
            continue;
        }
        if (!alphabet) {
            set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid base64 character");
            return HRTCORE_ERR_INVALID_ARG;
        }
    }

    const size_t needed = (in_len / 4) * 3;
    if (out_cap < needed) {
        set_error(err, HRTCORE_ERR_BUFFER_TOO_SMALL, "base64 decode buffer too small");
        return HRTCORE_ERR_BUFFER_TOO_SMALL;
    }

    const int written = EVP_DecodeBlock((unsigned char *)out, (const unsigned char *)in,
                                        (int)in_len);
    if (written < 0) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "base64 decode failed");
        return HRTCORE_ERR_INVALID_ARG;
    }

    size_t padding = 0;
    if (in[in_len - 1] == '=') {
        ++padding;
    }
    if (in_len >= 2 && in[in_len - 2] == '=') {
        ++padding;
    }
    *out_len = (size_t)written - padding;
    return HRTCORE_OK;
}
