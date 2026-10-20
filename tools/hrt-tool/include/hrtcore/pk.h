#ifndef HRTCORE_PK_H
#define HRTCORE_PK_H

#include "hrtcore/hrtcore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors logic.ts Route. */
typedef enum hrtcore_route {
    HRTCORE_ROUTE_SUBLINGUAL = 0,
    HRTCORE_ROUTE_INJECTION = 1,
    HRTCORE_ROUTE_PATCH_APPLY = 2,
    HRTCORE_ROUTE_PATCH_REMOVE = 3,
    HRTCORE_ROUTE_GEL = 4,
    HRTCORE_ROUTE_ORAL = 5
} hrtcore_route;

/* Mirrors logic.ts Ester. */
typedef enum hrtcore_ester {
    HRTCORE_ESTER_E2 = 0,
    HRTCORE_ESTER_EB,
    HRTCORE_ESTER_EV,
    HRTCORE_ESTER_EC,
    HRTCORE_ESTER_EN,
    HRTCORE_ESTER_EU,
    HRTCORE_ESTER_CPA,
    HRTCORE_ESTER_SPIRO,
    HRTCORE_ESTER_BICA,
    HRTCORE_ESTER_MPA,
    HRTCORE_ESTER_LEUP,
    HRTCORE_ESTER_TRIP,
    HRTCORE_ESTER_RLX,
    HRTCORE_ESTER_T,
    HRTCORE_ESTER_TC,
    HRTCORE_ESTER_TE,
    HRTCORE_ESTER_TU
} hrtcore_ester;

/* Mirrors logic.ts ExtraKey. */
typedef enum hrtcore_extra {
    HRTCORE_EXTRA_CONCENTRATION_MG_ML = 0,
    HRTCORE_EXTRA_AREA_CM2 = 1,
    HRTCORE_EXTRA_RELEASE_RATE_UG_PER_DAY = 2,
    HRTCORE_EXTRA_SUBLINGUAL_THETA = 3,
    HRTCORE_EXTRA_SUBLINGUAL_TIER = 4,
    HRTCORE_EXTRA_GEL_SITE = 5,
    HRTCORE_EXTRA_PATCH_WEAR_H = 6,
    HRTCORE_EXTRA_COUNT = 7
} hrtcore_extra;

typedef struct hrtcore_dose_event {
    char id[64];
    int32_t route;
    int32_t ester;
    double dose_mg;
    double time_h;
    uint32_t extras_present; /* bit i set when extras[i] is meaningful */
    double extras[HRTCORE_EXTRA_COUNT];
} hrtcore_dose_event;

typedef struct hrtcore_pk_params {
    double e2_kClear;
    double e2_kClearInj;
    double e2_ff_EB;
    double e2_ff_EV;
    double e2_ff_EC;
    double e2_ff_EN;
    double e2_ff_EU;
    double e2_oral_bio;
    double e2_sl_quick;
    double e2_sl_casual;
    double e2_sl_standard;
    double e2_sl_strict;
    double e2_gel_arm;
    double e2_gel_thigh;
    double e2_gel_scrotal;
    double t_kClear;
    double t_kClearInj;
    double t_ff_TC;
    double t_ff_TE;
    double t_ff_TU;
    double t_gel_arm;
    double t_gel_thigh;
    double t_gel_scrotal;
} hrtcore_pk_params;

typedef struct hrtcore_pk_result {
    double *time_h;
    double *e2;
    double *cpa;
    double *t;
    size_t length;
    double auc;
} hrtcore_pk_result;

void hrtcore_pk_defaults(hrtcore_pk_params *out);

/*
 * Runs the simulation with a caller-supplied "now_h" so results are
 * reproducible. `birth_year` applies logic.ts's age clearance adjustment to
 * each dose at the time it was taken; 0 means no year of birth, and values
 * below 1900 are treated the same way. The grid and the four arrays are
 * malloc'd; free them with hrtcore_pk_result_free. Returns
 * HRTCORE_ERR_NO_DATA when no modelled event remains or the weight is not
 * positive.
 */
int hrtcore_pk_run(const hrtcore_dose_event *events, size_t count, double weight_kg,
                   const hrtcore_pk_params *params, int32_t birth_year, double now_h,
                   hrtcore_pk_result *out, hrtcore_error *err);

void hrtcore_pk_result_free(hrtcore_pk_result *result);

double hrtcore_pk_interp_e2(const hrtcore_pk_result *result, double hour);
double hrtcore_pk_interp_cpa(const hrtcore_pk_result *result, double hour);
double hrtcore_pk_interp_t(const hrtcore_pk_result *result, double hour);

#ifdef __cplusplus
}
#endif

#endif /* HRTCORE_PK_H */
