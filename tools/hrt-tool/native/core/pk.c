#include "hrtcore/pk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hrtcore_pk_defaults(hrtcore_pk_params *out) {
    if (out == NULL) {
        return;
    }
    *out = (hrtcore_pk_params){
        .e2_kClear = 0.41,
        .e2_kClearInj = 0.041,
        .e2_ff_EB = 0.1092,
        .e2_ff_EV = 0.0623,
        .e2_ff_EC = 0.1173,
        .e2_ff_EN = 0.12,
        .e2_ff_EU = 0.04,
        .e2_oral_bio = 0.03,
        .e2_sl_quick = 0.01,
        .e2_sl_casual = 0.04,
        .e2_sl_standard = 0.11,
        .e2_sl_strict = 0.18,
        .e2_gel_arm = 0.05,
        .e2_gel_thigh = 0.05,
        .e2_gel_scrotal = 0.25,
        .t_kClear = 0.5,
        .t_kClearInj = 0.035,
        .t_ff_TC = 0.043,
        .t_ff_TE = 0.043,
        .t_ff_TU = 0.043,
        .t_gel_arm = 0.1,
        .t_gel_thigh = 0.1,
        .t_gel_scrotal = 0.5,
    };
}

int hrtcore_pk_run(const hrtcore_dose_event *events, size_t count, double weight_kg,
                   const hrtcore_pk_params *params, double now_h,
                   hrtcore_pk_result *out, hrtcore_error *err) {
    (void)events;
    (void)count;
    (void)weight_kg;
    (void)params;
    (void)now_h;
    (void)out;
    if (err != NULL) {
        err->code = HRTCORE_ERR_NOT_IMPLEMENTED;
        snprintf(err->message, sizeof(err->message),
                 "the PK engine is not implemented yet");
    }
    return HRTCORE_ERR_NOT_IMPLEMENTED;
}

void hrtcore_pk_result_free(hrtcore_pk_result *result) {
    if (result == NULL) {
        return;
    }
    free(result->time_h);
    free(result->e2);
    free(result->cpa);
    free(result->t);
    memset(result, 0, sizeof(*result));
}

double hrtcore_pk_interp_e2(const hrtcore_pk_result *result, double hour) {
    (void)result;
    (void)hour;
    return 0.0;
}

double hrtcore_pk_interp_cpa(const hrtcore_pk_result *result, double hour) {
    (void)result;
    (void)hour;
    return 0.0;
}

double hrtcore_pk_interp_t(const hrtcore_pk_result *result, double hour) {
    (void)result;
    (void)hour;
    return 0.0;
}
