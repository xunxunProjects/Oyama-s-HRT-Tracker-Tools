/*
 * C port of the pharmacokinetic engine in logic.ts.
 *
 * The reference is the TypeScript implementation, function for function:
 * resolveParams, PrecomputedEventModel, patchRemovalTimes,
 * computeMaxLifetimeH, runSimulation, and the three interpolators. The
 * reference vectors in testdata/pk-vectors.json are generated from it.
 */

#include "hrtcore/pk.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HRTCORE_LN2 0.6931471805599453
#define HRTCORE_MAX_GRID_STEPS 100000
#define HRTCORE_MAX_TIMELINE_POINTS 2000000
#define HRTCORE_PATCH_MIN_WEAR_H 1.0
#define HRTCORE_PATCH_REMOVAL_GRACE_H 24.0
#define HRTCORE_PATCH_MAX_OVERWEAR_H 240.0

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

static const double kCoreVdPerKg = 2.0;
static const double kCoreVdPerKgCpa = 14.0;
static const double kTCoreVdPerKg = 1.0;

static const double kMw[17] = {
    272.38, 376.5,  356.5,  396.58, 384.56, 440.66, 416.94, 416.57, 430.37,
    386.52, 1209.42, 1311.45, 473.58, 288.42, 412.6,  400.59, 456.7,
};

static const double kTDepotFracFast[3] = {0.35, 0.4, 0.0};
static const double kTDepotK1Fast[3] = {0.025, 0.035, HRTCORE_LN2 / (50.0 * 24.0)};
static const double kTDepotK1Slow[3] = {0.005, 0.008, HRTCORE_LN2 / (50.0 * 24.0)};
static const double kTEsterK2[3] = {0.2, 0.2, 0.036};

static const double kTGelK1 = 0.05;
static const double kTPatchK1 = 0.03;
static const double kTPatchF = 1.0;

static const double kTwoPartFracFast[6] = {1.0, 0.9, 0.4, 0.229164549, 0.05, 0.08};
static const double kTwoPartK1Fast[6] = {0.5, 0.144, 0.0216, 0.005035046, 0.001, 0.006};
static const double kTwoPartK1Slow[6] = {0.0, 0.114, 0.0138, 0.004510574, 0.005, 0.0022};
static const double kInjectionFormation[6] = {1.0, 0.1092, 0.0623, 0.1173, 0.12, 0.04};
static const double kEsterK2[6] = {0.0, 0.09, 0.07, 0.045, 0.015, 0.012};

static const double kOralKAbsE2 = 0.32;
static const double kOralKAbsEV = 0.05;
static const double kOralKE1Pool = HRTCORE_LN2 / 14.36;
static const double kOralKAbsSL = 1.8;

static const double kCpaAlpha = 0.25;
static const double kCpaBeta = HRTCORE_LN2 / 43.9;
static const double kCpaKa = 0.5;
static const double kCpaWFast = 5.2786;
static const double kCpaF = 0.88;

static const double kPeriEventOffsets[] = {0.25, 0.5, 1.0, 2.0, 4.0,
                                           6.0,  8.0, 12.0, 24.0, 48.0};

typedef struct {
    double frac_fast;
    double k1_fast;
    double k1_slow;
    double k2;
    double k2_slow;
    bool has_k2_slow;
    double k3;
    double f;
    double rate_mgh;
    double f_fast;
    double f_slow;
} pk_params_t;

typedef struct {
    size_t event_index;
    pk_params_t params;
    double wear_h;
    double max_lifetime_h;
} precomputed_t;

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static void set_error(hrtcore_error *err, int code, const char *message) {
    if (err == NULL) {
        return;
    }
    err->code = code;
    snprintf(err->message, sizeof(err->message), "%s", message);
}

static double extra_value(const hrtcore_dose_event *event, int index, double fallback) {
    if ((event->extras_present & (1u << index)) != 0u) {
        return event->extras[index];
    }
    return fallback;
}

static bool is_testosterone_ester(int ester) {
    return ester == HRTCORE_ESTER_T || ester == HRTCORE_ESTER_TC ||
           ester == HRTCORE_ESTER_TE || ester == HRTCORE_ESTER_TU;
}

static bool is_unmodelled_ester(int ester) {
    return ester < 0 || ester > HRTCORE_ESTER_TU || ester == HRTCORE_ESTER_SPIRO ||
           ester == HRTCORE_ESTER_BICA || ester == HRTCORE_ESTER_MPA ||
           ester == HRTCORE_ESTER_LEUP || ester == HRTCORE_ESTER_TRIP ||
           ester == HRTCORE_ESTER_RLX;
}

static bool is_estradiol_ester(int ester) {
    return !is_unmodelled_ester(ester) && !is_testosterone_ester(ester) &&
           ester != HRTCORE_ESTER_CPA;
}

static double get_to_e2_factor(int ester) {
    if (ester == HRTCORE_ESTER_E2) {
        return 1.0;
    }
    if (is_unmodelled_ester(ester) || ester == HRTCORE_ESTER_CPA) {
        return 1.0;
    }
    if (is_testosterone_ester(ester)) {
        return kMw[HRTCORE_ESTER_T] / kMw[ester];
    }
    return kMw[HRTCORE_ESTER_E2] / kMw[ester];
}

static double get_e2_inj_ff(const hrtcore_pk_params *params, int ester) {
    switch (ester) {
    case HRTCORE_ESTER_EB: return params->e2_ff_EB;
    case HRTCORE_ESTER_EV: return params->e2_ff_EV;
    case HRTCORE_ESTER_EC: return params->e2_ff_EC;
    case HRTCORE_ESTER_EN: return params->e2_ff_EN;
    case HRTCORE_ESTER_EU: return params->e2_ff_EU;
    case HRTCORE_ESTER_E2: return kInjectionFormation[0];
    default: return 0.08;
    }
}

static double get_t_inj_ff(const hrtcore_pk_params *params, int ester) {
    if (ester == HRTCORE_ESTER_TC) return params->t_ff_TC;
    if (ester == HRTCORE_ESTER_TE) return params->t_ff_TE;
    if (ester == HRTCORE_ESTER_TU) return params->t_ff_TU;
    return params->t_ff_TE;
}

static int gel_site_index(const hrtcore_dose_event *event) {
    const double raw = extra_value(event, HRTCORE_EXTRA_GEL_SITE, 0.0);
    int index = (int)llround(raw);
    if (index < 0) index = 0;
    if (index > 2) index = 2;
    return index;
}

static double get_gel_bio(const hrtcore_pk_params *params, int site) {
    if (site == 0) return params->e2_gel_arm;
    if (site == 1) return params->e2_gel_thigh;
    if (site == 2) return params->e2_gel_scrotal;
    return params->e2_gel_arm;
}

static double get_t_gel_bio(const hrtcore_pk_params *params, int site) {
    if (site == 0) return params->t_gel_arm;
    if (site == 1) return params->t_gel_thigh;
    if (site == 2) return params->t_gel_scrotal;
    return params->t_gel_arm;
}

static double get_sl_theta(const hrtcore_pk_params *params, int tier) {
    if (tier == 0) return params->e2_sl_quick;
    if (tier == 1) return params->e2_sl_casual;
    if (tier == 2) return params->e2_sl_standard;
    return params->e2_sl_strict;
}

static int sl_tier_index(const hrtcore_dose_event *event) {
    const double raw = extra_value(event, HRTCORE_EXTRA_SUBLINGUAL_TIER, 2.0);
    int index = (int)llround(raw);
    if (index < 0) index = 0;
    if (index > 3) index = 3;
    return index;
}

static double sublingual_theta(const hrtcore_dose_event *event,
                               const hrtcore_pk_params *params) {
    if ((event->extras_present & (1u << HRTCORE_EXTRA_SUBLINGUAL_THETA)) != 0u) {
        double theta = event->extras[HRTCORE_EXTRA_SUBLINGUAL_THETA];
        if (isfinite(theta)) {
            if (theta < 0.0) theta = 0.0;
            if (theta > 1.0) theta = 1.0;
            return theta;
        }
    }
    if ((event->extras_present & (1u << HRTCORE_EXTRA_SUBLINGUAL_TIER)) != 0u) {
        return get_sl_theta(params, sl_tier_index(event));
    }
    return params->e2_sl_standard;
}

static double get_bioavailability(const hrtcore_dose_event *event,
                                  const hrtcore_pk_params *params) {
    const double mw_factor = get_to_e2_factor(event->ester);

    if (event->ester == HRTCORE_ESTER_CPA) {
        return event->route == HRTCORE_ROUTE_ORAL ? kCpaF : 0.0;
    }

    if (is_testosterone_ester(event->ester)) {
        switch (event->route) {
        case HRTCORE_ROUTE_INJECTION:
            if (event->ester == HRTCORE_ESTER_T) return 0.0;
            return get_t_inj_ff(params, event->ester) * mw_factor;
        case HRTCORE_ROUTE_GEL: {
            const int site = gel_site_index(event);
            return get_t_gel_bio(params, site) * mw_factor;
        }
        case HRTCORE_ROUTE_PATCH_APPLY:
            return kTPatchF * mw_factor;
        default:
            return 0.0;
        }
    }

    switch (event->route) {
    case HRTCORE_ROUTE_INJECTION:
        return get_e2_inj_ff(params, event->ester) * mw_factor;
    case HRTCORE_ROUTE_ORAL:
        return params->e2_oral_bio * mw_factor;
    case HRTCORE_ROUTE_SUBLINGUAL: {
        const double theta = sublingual_theta(event, params);
        return (theta + (1.0 - theta) * params->e2_oral_bio) * mw_factor;
    }
    case HRTCORE_ROUTE_GEL: {
        const int site = gel_site_index(event);
        return get_gel_bio(params, site) * mw_factor;
    }
    case HRTCORE_ROUTE_PATCH_APPLY:
        return 1.0 * mw_factor;
    default:
        return 0.0;
    }
}

/* ------------------------------------------------------------------ */
/* Analytic kernels                                                   */
/* ------------------------------------------------------------------ */

static double exp_div_diff1(double x, double y, double tau) {
    const double z = (fabs(y - x) * tau) / 2.0;
    if (z > 1.0) {
        return (exp(-x * tau) - exp(-y * tau)) / (x - y);
    }
    const double sinhc = z == 0.0 ? 1.0 : sinh(z) / z;
    return -tau * exp((-(x + y) / 2.0) * tau) * sinhc;
}

static double exp_div_diff2(double a, double b, double c, double tau) {
    if (a > b) {
        const double t = a;
        a = b;
        b = t;
    }
    if (b > c) {
        const double t = b;
        b = c;
        c = t;
    }
    if (a > b) {
        const double t = a;
        a = b;
        b = t;
    }
    if ((c - a) * tau >= 0.5) {
        return (exp_div_diff1(b, c, tau) - exp_div_diff1(a, b, tau)) / (c - a);
    }
    const double mean = (a + b + c) / 3.0;
    const double ua = (mean - a) * tau;
    const double ub = (mean - b) * tau;
    const double uc = (mean - c) * tau;
    double h1 = 1.0;
    double h2 = 1.0;
    double h3 = 1.0;
    double fact = 2.0;
    double sum = 0.5;
    for (int j = 1; j < 20; ++j) {
        h1 *= ua;
        h2 = h1 + ub * h2;
        h3 = h2 + uc * h3;
        fact *= (double)(j + 2);
        sum += h3 / fact;
    }
    return tau * tau * exp(-mean * tau) * sum;
}

static double analytic2c(double tau, double dose_mg, double f, double ka, double ke) {
    if (ka <= 0.0 || dose_mg <= 0.0) {
        return 0.0;
    }
    return -dose_mg * f * ka * exp_div_diff1(ka, ke, tau);
}

static double analytic3c(double tau, double dose_mg, double f, double k1, double k2,
                         double k3) {
    if (k1 <= 0.0 || dose_mg <= 0.0) {
        return 0.0;
    }
    return dose_mg * f * k1 * k2 * exp_div_diff2(k1, k2, k3, tau);
}

static double one_comp_amount(double tau, double dose_mg, const pk_params_t *params) {
    return analytic2c(tau, dose_mg, params->f, params->k1_fast, params->k3);
}

/* ------------------------------------------------------------------ */
/* Route parameter resolution                                         */
/* ------------------------------------------------------------------ */

static void zero_params(pk_params_t *out, double k3) {
    memset(out, 0, sizeof(*out));
    out->k3 = k3;
    out->k2_slow = 0.0;
}

/* Mirrors ageClearanceMultiplier in logic.ts. The slope is per year of age;
   outside 19..75 the multiplier holds at its end value, and 0 (or an
   implausible year) means no adjustment. */
static double age_clearance_multiplier(bool testosterone, int32_t birth_year,
                                       double time_h) {
    if (birth_year < 1900) {
        return 1.0;
    }
    const double hours_per_year = 365.2425 * 24.0;
    double age = 1970.0 + time_h / hours_per_year -
                 ((double)birth_year + 0.5);
    if (age < 19.0) {
        age = 19.0;
    } else if (age > 75.0) {
        age = 75.0;
    }
    const double slope = testosterone ? log(1821.0 / 1390.0) / 40.0
                                      : log(1.0 / 0.75) / 52.0;
    return exp(-slope * (age - 54.0));
}

static pk_params_t resolve_params(const hrtcore_dose_event *event,
                                  const hrtcore_pk_params *params,
                                  int32_t birth_year) {
    const double default_k3 =
        (event->route == HRTCORE_ROUTE_INJECTION ? params->e2_kClearInj
                                                 : params->e2_kClear) *
        age_clearance_multiplier(false, birth_year, event->time_h);
    const double to_e2 = get_to_e2_factor(event->ester);
    pk_params_t out;
    memset(&out, 0, sizeof(out));

    if (is_testosterone_ester(event->ester)) {
        const double t_k3 =
            (event->route == HRTCORE_ROUTE_INJECTION ? params->t_kClearInj
                                                     : params->t_kClear) *
            age_clearance_multiplier(true, birth_year, event->time_h);
        if (event->route == HRTCORE_ROUTE_INJECTION) {
            if (event->ester == HRTCORE_ESTER_T) {
                zero_params(&out, t_k3);
                return out;
            }
            int idx = 0;
            if (event->ester == HRTCORE_ESTER_TE) idx = 1;
            if (event->ester == HRTCORE_ESTER_TU) idx = 2;
            out.frac_fast = kTDepotFracFast[idx];
            out.k1_fast = kTDepotK1Fast[idx];
            out.k1_slow = kTDepotK1Slow[idx];
            out.k2 = kTEsterK2[idx];
            out.k3 = t_k3;
            out.f = get_bioavailability(event, params);
            out.f_fast = out.f;
            out.f_slow = out.f;
            return out;
        }
        if (event->route == HRTCORE_ROUTE_GEL) {
            out.frac_fast = 1.0;
            out.k1_fast = kTGelK1;
            out.k3 = t_k3;
            out.f = get_bioavailability(event, params);
            out.f_fast = out.f;
            out.f_slow = out.f;
            return out;
        }
        if (event->route == HRTCORE_ROUTE_PATCH_APPLY) {
            out.frac_fast = 1.0;
            out.k3 = t_k3;
            out.f = get_bioavailability(event, params);
            out.f_fast = out.f;
            out.f_slow = out.f;
            const double release = extra_value(event, HRTCORE_EXTRA_RELEASE_RATE_UG_PER_DAY,
                                               -1.0);
            if (isfinite(release) && release > 0.0) {
                out.rate_mgh = (release / 24.0 / 1000.0) * out.f;
            } else {
                out.k1_fast = kTPatchK1;
            }
            return out;
        }
        zero_params(&out, t_k3);
        return out;
    }

    if (event->ester == HRTCORE_ESTER_CPA && event->route != HRTCORE_ROUTE_ORAL) {
        zero_params(&out, default_k3);
        return out;
    }

    switch (event->route) {
    case HRTCORE_ROUTE_INJECTION: {
        int idx = -1;
        switch (event->ester) {
        case HRTCORE_ESTER_E2: idx = 0; break;
        case HRTCORE_ESTER_EB: idx = 1; break;
        case HRTCORE_ESTER_EV: idx = 2; break;
        case HRTCORE_ESTER_EC: idx = 3; break;
        case HRTCORE_ESTER_EN: idx = 4; break;
        case HRTCORE_ESTER_EU: idx = 5; break;
        default: break;
        }
        out.frac_fast = idx >= 0 ? kTwoPartFracFast[idx] : 0.5;
        out.k1_fast = idx >= 0 ? kTwoPartK1Fast[idx] : 0.1;
        out.k1_slow = idx >= 0 ? kTwoPartK1Slow[idx] : 0.01;
        out.k2 = idx >= 0 ? kEsterK2[idx] : 0.0;
        out.k3 = default_k3;
        out.f = get_bioavailability(event, params);
        out.f_fast = out.f;
        out.f_slow = out.f;
        return out;
    }
    case HRTCORE_ROUTE_SUBLINGUAL: {
        const double theta = sublingual_theta(event, params);
        out.frac_fast = theta;
        out.k1_fast = kOralKAbsSL;
        out.k1_slow = event->ester == HRTCORE_ESTER_EV ? kOralKAbsEV : kOralKAbsE2;
        out.k2 = kEsterK2[event->ester];
        out.has_k2_slow = event->ester != HRTCORE_ESTER_EV;
        out.k2_slow = event->ester == HRTCORE_ESTER_EV ? 0.0 : kOralKE1Pool;
        out.f_fast = to_e2;
        out.f_slow = params->e2_oral_bio * to_e2;
        out.f = theta * out.f_fast + (1.0 - theta) * out.f_slow;
        out.k3 = default_k3;
        return out;
    }
    case HRTCORE_ROUTE_GEL:
        out.frac_fast = 1.0;
        out.k1_fast = 0.0193;
        out.k3 = default_k3;
        out.f = get_bioavailability(event, params);
        out.f_fast = out.f;
        out.f_slow = out.f;
        return out;
    case HRTCORE_ROUTE_PATCH_APPLY: {
        out.frac_fast = 1.0;
        out.k3 = default_k3;
        out.f = get_bioavailability(event, params);
        out.f_fast = out.f;
        out.f_slow = out.f;
        const double release = extra_value(event, HRTCORE_EXTRA_RELEASE_RATE_UG_PER_DAY, -1.0);
        if (isfinite(release) && release > 0.0) {
            out.rate_mgh = (release / 24.0 / 1000.0) * out.f;
        } else {
            out.k1_fast = 0.0075;
        }
        return out;
    }
    case HRTCORE_ROUTE_PATCH_REMOVE:
        zero_params(&out, default_k3);
        return out;
    case HRTCORE_ROUTE_ORAL:
        if (event->ester == HRTCORE_ESTER_CPA) {
            out.frac_fast = 1.0;
            out.k1_fast = kCpaKa;
            out.k3 = kCpaBeta;
            out.f = kCpaF;
            out.f_fast = kCpaF;
            out.f_slow = kCpaF;
            return out;
        }
        out.frac_fast = 1.0;
        out.k1_fast = event->ester == HRTCORE_ESTER_EV ? kOralKAbsEV : kOralKAbsE2;
        out.k2 = event->ester == HRTCORE_ESTER_EV ? 0.0 : kOralKE1Pool;
        out.k3 = default_k3;
        out.f = params->e2_oral_bio * to_e2;
        out.f_fast = out.f;
        out.f_slow = out.f;
        return out;
    default:
        zero_params(&out, default_k3);
        return out;
    }
}

/* ------------------------------------------------------------------ */
/* Patch pairing                                                      */
/* ------------------------------------------------------------------ */

static double planned_wear_h(const hrtcore_dose_event *event) {
    if ((event->extras_present & (1u << HRTCORE_EXTRA_PATCH_WEAR_H)) != 0u) {
        const double planned = event->extras[HRTCORE_EXTRA_PATCH_WEAR_H];
        if (isfinite(planned) && planned > 0.0) {
            return planned;
        }
    }
    return INFINITY;
}

/* 0 = e2, 1 = t, -1 = neither. */
static int patch_hormone(int ester) {
    if (is_testosterone_ester(ester)) return 1;
    if (is_estradiol_ester(ester)) return 0;
    return -1;
}

typedef int (*index_cmp_fn)(size_t a, size_t b, const void *context);

static void merge_sort_indices(size_t *values, size_t *scratch, size_t lo, size_t hi,
                               index_cmp_fn compare, const void *context) {
    if (hi - lo < 2) {
        return;
    }
    const size_t mid = lo + (hi - lo) / 2;
    merge_sort_indices(values, scratch, lo, mid, compare, context);
    merge_sort_indices(values, scratch, mid, hi, compare, context);
    size_t i = lo;
    size_t j = mid;
    size_t k = lo;
    while (i < mid && j < hi) {
        if (compare(values[j], values[i], context) < 0) {
            scratch[k++] = values[j++];
        } else {
            scratch[k++] = values[i++];
        }
    }
    while (i < mid) scratch[k++] = values[i++];
    while (j < hi) scratch[k++] = values[j++];
    memcpy(values + lo, scratch + lo, (hi - lo) * sizeof(size_t));
}

typedef struct {
    const hrtcore_dose_event *events;
} event_context;

static int compare_time(size_t a, size_t b, const void *context) {
    const hrtcore_dose_event *events = ((const event_context *)context)->events;
    if (events[a].time_h < events[b].time_h) return -1;
    if (events[a].time_h > events[b].time_h) return 1;
    return 0;
}

static const char *const kEsterCodes[17] = {
    "E2", "EB", "EV", "EC", "EN", "EU", "CPA", "SPIRO", "BICA",
    "MPA", "LEUP", "TRIP", "RLX", "T", "TC", "TE", "TU",
};

static int compare_apply(size_t a, size_t b, const void *context) {
    const hrtcore_dose_event *events = ((const event_context *)context)->events;
    const hrtcore_dose_event *left = &events[a];
    const hrtcore_dose_event *right = &events[b];
    if (left->time_h != right->time_h) return left->time_h < right->time_h ? -1 : 1;
    const double left_wear = planned_wear_h(left);
    const double right_wear = planned_wear_h(right);
    if (left_wear != right_wear) return left_wear < right_wear ? -1 : 1;
    if (left->dose_mg != right->dose_mg) return left->dose_mg < right->dose_mg ? -1 : 1;
    const double left_rate = extra_value(left, HRTCORE_EXTRA_RELEASE_RATE_UG_PER_DAY, 0.0);
    const double right_rate = extra_value(right, HRTCORE_EXTRA_RELEASE_RATE_UG_PER_DAY, 0.0);
    if (left_rate != right_rate) return left_rate < right_rate ? -1 : 1;
    const int ester_cmp = strcmp(kEsterCodes[left->ester], kEsterCodes[right->ester]);
    if (ester_cmp != 0) return ester_cmp;
    return strcmp(left->id, right->id);
}

static int compare_removal(size_t a, size_t b, const void *context) {
    const hrtcore_dose_event *events = ((const event_context *)context)->events;
    if (events[a].time_h != events[b].time_h) {
        return events[a].time_h < events[b].time_h ? -1 : 1;
    }
    const int left_rank = patch_hormone(events[a].ester) == 0
                              ? 0
                              : (patch_hormone(events[a].ester) == 1 ? 1 : 2);
    const int right_rank = patch_hormone(events[b].ester) == 0
                               ? 0
                               : (patch_hormone(events[b].ester) == 1 ? 1 : 2);
    if (left_rank != right_rank) return left_rank < right_rank ? -1 : 1;
    return 0;
}

static void patch_removal_times(const hrtcore_dose_event *events, size_t count,
                                double *removal_times) {
    for (size_t i = 0; i < count; ++i) {
        removal_times[i] = NAN;
    }

    size_t *applies = malloc(count * sizeof(size_t));
    size_t *removals = malloc(count * sizeof(size_t));
    size_t *scratch = malloc(count * sizeof(size_t));
    size_t *open = malloc(count * sizeof(size_t));
    if (applies == NULL || removals == NULL || scratch == NULL || open == NULL) {
        free(applies);
        free(removals);
        free(scratch);
        free(open);
        return;
    }

    size_t apply_count = 0;
    size_t removal_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (events[i].route == HRTCORE_ROUTE_PATCH_APPLY) {
            applies[apply_count++] = i;
        } else if (events[i].route == HRTCORE_ROUTE_PATCH_REMOVE) {
            removals[removal_count++] = i;
        }
    }

    const event_context context = {events};
    merge_sort_indices(applies, scratch, 0, apply_count, compare_apply, &context);
    merge_sort_indices(removals, scratch, 0, removal_count, compare_removal, &context);

    size_t open_len = 0;
    size_t next = 0;
    for (size_t r = 0; r < removal_count; ++r) {
        const size_t removal = removals[r];
        const double removal_time = events[removal].time_h;
        while (next < apply_count &&
               events[applies[next]].time_h <= removal_time - HRTCORE_PATCH_MIN_WEAR_H) {
            open[open_len++] = applies[next++];
        }

        size_t kept = 0;
        for (size_t j = 0; j < open_len; ++j) {
            const size_t apply = open[j];
            const double wear = planned_wear_h(&events[apply]);
            if (!isfinite(wear) ||
                events[apply].time_h + wear + HRTCORE_PATCH_REMOVAL_GRACE_H >= removal_time) {
                open[kept++] = apply;
            }
        }
        open_len = kept;

        const int hormone = patch_hormone(events[removal].ester);
        size_t match = SIZE_MAX;
        for (size_t j = 0; j < open_len; ++j) {
            if (hormone < 0 || patch_hormone(events[open[j]].ester) == hormone) {
                match = j;
                break;
            }
        }
        if (match != SIZE_MAX) {
            removal_times[open[match]] = removal_time;
            /* Order-preserving removal, like Array.prototype.splice. */
            memmove(&open[match], &open[match + 1],
                    (open_len - match - 1) * sizeof(size_t));
            --open_len;
            continue;
        }

        /*
         * No patch fits by its plan. An explicit removal still ends a patch
         * worn past its planned wear, so it goes to the newest patch of that
         * hormone put on before it, if that one is unpaired and within the
         * overwear window. An older patch is never taken: a newer one
         * replaced it. Patches put on at the same moment are tried in the
         * order the plan match uses.
         */
        size_t last = next;
        while (last > 0 &&
               (hormone >= 0 &&
                patch_hormone(events[applies[last - 1]].ester) != hormone)) {
            --last;
        }
        if (last == 0) {
            continue;
        }
        const size_t last_index = last - 1;
        size_t first = last_index;
        while (first > 0 &&
               events[applies[first - 1]].time_h ==
                   events[applies[last_index]].time_h) {
            --first;
        }
        for (size_t i = first; i <= last_index; ++i) {
            const size_t apply = applies[i];
            if (hormone >= 0 && patch_hormone(events[apply].ester) != hormone) {
                continue;
            }
            if (!isnan(removal_times[apply])) {
                continue;
            }
            double planned = planned_wear_h(&events[apply]);
            if (!isfinite(planned)) {
                planned = 0.0;
            }
            if (removal_time > events[apply].time_h + planned + HRTCORE_PATCH_MAX_OVERWEAR_H) {
                continue;
            }
            removal_times[apply] = removal_time;
            break;
        }
    }

    free(applies);
    free(removals);
    free(scratch);
    free(open);
}

static double resolve_patch_wear_h(const hrtcore_dose_event *event, size_t index,
                                   const double *removal_times) {
    const double removal = removal_times[index];
    if (isfinite(removal) && removal > event->time_h) {
        return removal - event->time_h;
    }
    return planned_wear_h(event);
}

/* ------------------------------------------------------------------ */
/* Event amount                                                       */
/* ------------------------------------------------------------------ */

static double event_amount(const hrtcore_dose_event *event, const pk_params_t *params,
                           double wear_h, double time_h) {
    const double tau = time_h - event->time_h;
    if (tau < 0.0) {
        return 0.0;
    }
    const double dose = event->dose_mg;

    switch (event->route) {
    case HRTCORE_ROUTE_INJECTION: {
        const double dose_fast = dose * params->frac_fast;
        const double dose_slow = dose * (1.0 - params->frac_fast);
        if (params->k2 > 0.0) {
            return analytic3c(tau, dose_fast, params->f, params->k1_fast, params->k2,
                              params->k3) +
                   analytic3c(tau, dose_slow, params->f, params->k1_slow, params->k2,
                              params->k3);
        }
        return analytic2c(tau, dose_fast, params->f, params->k1_fast, params->k3) +
               analytic2c(tau, dose_slow, params->f, params->k1_slow, params->k3);
    }
    case HRTCORE_ROUTE_GEL:
    case HRTCORE_ROUTE_ORAL:
        if (event->route == HRTCORE_ROUTE_ORAL && event->ester == HRTCORE_ESTER_CPA) {
            const double exposure_h = 14.0 / (0.0035 * 60.0);
            const double w_slow = (exposure_h - kCpaWFast / kCpaAlpha) * kCpaBeta;
            return analytic2c(tau, dose, kCpaF * kCpaWFast, kCpaKa, kCpaAlpha) +
                   analytic2c(tau, dose, kCpaF * w_slow, kCpaKa, kCpaBeta);
        }
        if (params->k2 > 0.0) {
            return analytic3c(tau, dose, params->f, params->k1_fast, params->k2, params->k3);
        }
        return one_comp_amount(tau, dose, params);
    case HRTCORE_ROUTE_SUBLINGUAL: {
        const double dose_fast = dose * params->frac_fast;
        const double dose_slow = dose * (1.0 - params->frac_fast);
        const double fast = params->k2 > 0.0
                                ? analytic3c(tau, dose_fast, params->f_fast, params->k1_fast,
                                             params->k2, params->k3)
                                : analytic2c(tau, dose_fast, params->f_fast, params->k1_fast,
                                             params->k3);
        const double slow =
            params->has_k2_slow && params->k2_slow > 0.0
                ? analytic3c(tau, dose_slow, params->f_slow, params->k1_slow,
                             params->k2_slow, params->k3)
                : analytic2c(tau, dose_slow, params->f_slow, params->k1_slow, params->k3);
        return fast + slow;
    }
    case HRTCORE_ROUTE_PATCH_APPLY:
        if (params->rate_mgh > 0.0) {
            if (tau <= wear_h) {
                return (params->rate_mgh / params->k3) *
                       (1.0 - exp(-params->k3 * tau));
            }
            const double removed =
                (params->rate_mgh / params->k3) * (1.0 - exp(-params->k3 * wear_h));
            return removed * exp(-params->k3 * (tau - wear_h));
        }
        if (tau > wear_h && isfinite(wear_h)) {
            const double at_removal = one_comp_amount(wear_h, dose, params);
            return at_removal * exp(-params->k3 * (tau - wear_h));
        }
        return one_comp_amount(tau, dose, params);
    default:
        return 0.0;
    }
}

static double compute_max_lifetime_h(const pk_params_t *params,
                                     const hrtcore_dose_event *event, double wear_h) {
    if (event->route == HRTCORE_ROUTE_PATCH_APPLY) {
        if (!isfinite(wear_h)) {
            return INFINITY;
        }
        const double decay = params->k3 > 0.0 ? ceil(13.816 / params->k3) : 10000.0;
        return wear_h + decay;
    }
    double min_rate = INFINITY;
    if (params->k1_fast > 0.0 && params->k1_fast < min_rate) min_rate = params->k1_fast;
    if (params->k1_slow > 0.0 && params->k1_slow < min_rate) min_rate = params->k1_slow;
    if (params->k2 > 0.0 && params->k2 < min_rate) min_rate = params->k2;
    if (params->has_k2_slow && params->k2_slow > 0.0 && params->k2_slow < min_rate) {
        min_rate = params->k2_slow;
    }
    if (params->k3 > 0.0 && params->k3 < min_rate) min_rate = params->k3;
    if (!isfinite(min_rate)) {
        return INFINITY;
    }
    return ceil(13.816 / min_rate);
}

/* ------------------------------------------------------------------ */
/* Simulation                                                         */
/* ------------------------------------------------------------------ */

static int compare_double(const void *left, const void *right) {
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

static double interpolate_series(const hrtcore_pk_result *result, const double *series,
                                 double hour) {
    if (result == NULL || result->length == 0 || series == NULL) {
        return 0.0;
    }
    const double *times = result->time_h;
    if (hour <= times[0]) {
        return series[0];
    }
    if (hour >= times[result->length - 1]) {
        return series[result->length - 1];
    }
    size_t low = 0;
    size_t high = result->length - 1;
    while (high - low > 1) {
        const size_t mid = low + (high - low) / 2;
        if (times[mid] == hour) {
            return series[mid];
        }
        if (times[mid] < hour) {
            low = mid;
        } else {
            high = mid;
        }
    }
    if (times[high] == times[low]) {
        return series[low];
    }
    const double ratio = (hour - times[low]) / (times[high] - times[low]);
    return series[low] + (series[high] - series[low]) * ratio;
}

int hrtcore_pk_run(const hrtcore_dose_event *events, size_t count, double weight_kg,
                   const hrtcore_pk_params *params, int32_t birth_year, double now_h,
                   hrtcore_pk_result *out, hrtcore_error *err) {
    if (out == NULL || params == NULL || (events == NULL && count != 0)) {
        set_error(err, HRTCORE_ERR_INVALID_ARG, "invalid PK argument");
        return HRTCORE_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (count > 100000) {
        set_error(err, HRTCORE_ERR_LIMIT, "too many dose events");
        return HRTCORE_ERR_LIMIT;
    }
    /* The documented input bounds; reject rather than propagate NaN or
       absurd concentrations into the result. */
    if (!isfinite(weight_kg) || weight_kg < 20.0 || weight_kg > 400.0) {
        set_error(err, HRTCORE_ERR_INVALID_ARG,
                  "body weight must be between 20 and 400 kg");
        return HRTCORE_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(events[i].time_h) || events[i].time_h < 0.0 ||
            events[i].time_h > 1500000.0) {
            set_error(err, HRTCORE_ERR_INVALID_ARG,
                      "dose time is outside 0..1500000 hours");
            return HRTCORE_ERR_INVALID_ARG;
        }
        if (!isfinite(events[i].dose_mg) || events[i].dose_mg < 0.0 ||
            events[i].dose_mg > 10000.0) {
            set_error(err, HRTCORE_ERR_INVALID_ARG,
                      "dose amount is outside 0..10000 mg");
            return HRTCORE_ERR_INVALID_ARG;
        }
        for (size_t j = 0; j < HRTCORE_EXTRA_COUNT; ++j) {
            if ((events[i].extras_present & (1u << j)) != 0u &&
                !isfinite(events[i].extras[j])) {
                set_error(err, HRTCORE_ERR_INVALID_ARG,
                          "dose extra is not finite");
                return HRTCORE_ERR_INVALID_ARG;
            }
        }
    }

    size_t *indices = malloc(count * sizeof(size_t));
    size_t *scratch = malloc(count * sizeof(size_t));
    if ((indices == NULL || scratch == NULL) && count != 0) {
        free(indices);
        free(scratch);
        set_error(err, HRTCORE_ERR_LIMIT, "out of memory");
        return HRTCORE_ERR_LIMIT;
    }
    size_t modelled = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!is_unmodelled_ester(events[i].ester)) {
            indices[modelled++] = i;
        }
    }
    if (modelled == 0 || weight_kg <= 0.0) {
        free(indices);
        free(scratch);
        set_error(err, HRTCORE_ERR_NO_DATA, "no modelled dose events");
        return HRTCORE_ERR_NO_DATA;
    }

    const event_context context = {events};
    merge_sort_indices(indices, scratch, 0, modelled, compare_time, &context);
    free(scratch);

    /* Copy events into sorted order so all later indexing is simple. */
    hrtcore_dose_event *sorted = malloc(modelled * sizeof(hrtcore_dose_event));
    if (sorted == NULL) {
        free(indices);
        set_error(err, HRTCORE_ERR_LIMIT, "out of memory");
        return HRTCORE_ERR_LIMIT;
    }
    for (size_t i = 0; i < modelled; ++i) {
        sorted[i] = events[indices[i]];
    }

    double *removal_times = malloc(modelled * sizeof(double));
    precomputed_t *precomputed = malloc(modelled * sizeof(precomputed_t));
    if (removal_times == NULL || precomputed == NULL) {
        free(indices);
        free(sorted);
        free(removal_times);
        free(precomputed);
        set_error(err, HRTCORE_ERR_LIMIT, "out of memory");
        return HRTCORE_ERR_LIMIT;
    }
    patch_removal_times(sorted, modelled, removal_times);

    size_t precomputed_count = 0;
    for (size_t i = 0; i < modelled; ++i) {
        if (sorted[i].route == HRTCORE_ROUTE_PATCH_REMOVE) {
            continue;
        }
        precomputed_t entry;
        entry.event_index = i;
        entry.params = resolve_params(&sorted[i], params, birth_year);
        entry.wear_h = resolve_patch_wear_h(&sorted[i], i, removal_times);
        entry.max_lifetime_h =
            compute_max_lifetime_h(&entry.params, &sorted[i], entry.wear_h);
        precomputed[precomputed_count++] = entry;
    }

    const double start_time = sorted[0].time_h - 24.0;
    const double end_time =
        fmax(sorted[modelled - 1].time_h + 24.0 * 14.0, now_h + 24.0);
    const double total_hours = end_time - start_time;
    size_t steps = (size_t)ceil(total_hours);
    if (steps < 2000) steps = 2000;
    if (steps > HRTCORE_MAX_GRID_STEPS) steps = HRTCORE_MAX_GRID_STEPS;

    const double plasma_volume_e2 = kCoreVdPerKg * weight_kg * 1000.0;
    const double plasma_volume_cpa = kCoreVdPerKgCpa * weight_kg * 1000.0;
    const double plasma_volume_t = kTCoreVdPerKg * weight_kg * 1000.0;

    const size_t peri_capacity = precomputed_count * (sizeof(kPeriEventOffsets) /
                                                      sizeof(kPeriEventOffsets[0]));
    size_t capacity = steps + modelled + peri_capacity + 1;
    if (capacity > HRTCORE_MAX_TIMELINE_POINTS) {
        free(indices);
        free(sorted);
        free(removal_times);
        free(precomputed);
        set_error(err, HRTCORE_ERR_LIMIT, "PK timeline is too large");
        return HRTCORE_ERR_LIMIT;
    }
    double *timeline = malloc(capacity * sizeof(double));
    if (timeline == NULL) {
        free(indices);
        free(sorted);
        free(removal_times);
        free(precomputed);
        set_error(err, HRTCORE_ERR_LIMIT, "out of memory");
        return HRTCORE_ERR_LIMIT;
    }

    size_t timeline_len = 0;
    const double step_size = (end_time - start_time) / (double)(steps - 1);
    for (size_t i = 0; i < steps; ++i) {
        timeline[timeline_len++] = start_time + (double)i * step_size;
    }
    for (size_t i = 0; i < modelled; ++i) {
        timeline[timeline_len++] = sorted[i].time_h;
    }
    for (size_t i = 0; i < precomputed_count; ++i) {
        for (size_t j = 0; j < sizeof(kPeriEventOffsets) / sizeof(kPeriEventOffsets[0]);
             ++j) {
            const double offset = kPeriEventOffsets[j];
            if (offset > precomputed[i].max_lifetime_h) {
                break;
            }
            const double t = sorted[precomputed[i].event_index].time_h + offset;
            if (t >= start_time && t <= end_time) {
                timeline[timeline_len++] = t;
            }
        }
    }
    if (now_h >= start_time && now_h <= end_time) {
        timeline[timeline_len++] = now_h;
    }

    qsort(timeline, timeline_len, sizeof(double), compare_double);
    size_t unique_len = 0;
    for (size_t i = 0; i < timeline_len; ++i) {
        if (unique_len == 0 || timeline[i] != timeline[unique_len - 1]) {
            timeline[unique_len++] = timeline[i];
        }
    }
    timeline_len = unique_len;

    double *time_h = malloc(timeline_len * sizeof(double));
    double *e2 = malloc(timeline_len * sizeof(double));
    double *cpa = malloc(timeline_len * sizeof(double));
    double *t = malloc(timeline_len * sizeof(double));
    size_t *active = malloc((precomputed_count + 1) * sizeof(size_t));
    if (time_h == NULL || e2 == NULL || cpa == NULL || t == NULL || active == NULL) {
        free(indices);
        free(sorted);
        free(removal_times);
        free(precomputed);
        free(timeline);
        free(time_h);
        free(e2);
        free(cpa);
        free(t);
        free(active);
        set_error(err, HRTCORE_ERR_LIMIT, "out of memory");
        return HRTCORE_ERR_LIMIT;
    }

    size_t activate = 0;
    size_t active_len = 0;
    double auc = 0.0;
    for (size_t i = 0; i < timeline_len; ++i) {
        const double point = timeline[i];
        while (activate < precomputed_count &&
               sorted[precomputed[activate].event_index].time_h <= point) {
            active[active_len++] = activate++;
        }
        for (size_t j = 0; j < active_len;) {
            const precomputed_t *entry = &precomputed[active[j]];
            if (point - sorted[entry->event_index].time_h > entry->max_lifetime_h) {
                active[j] = active[active_len - 1];
                --active_len;
            } else {
                ++j;
            }
        }

        double total_e2_mg = 0.0;
        double total_cpa_mg = 0.0;
        double total_t_mg = 0.0;
        for (size_t j = 0; j < active_len; ++j) {
            const precomputed_t *entry = &precomputed[active[j]];
            const hrtcore_dose_event *event = &sorted[entry->event_index];
            const double amount = event_amount(event, &entry->params, entry->wear_h, point);
            if (event->ester == HRTCORE_ESTER_CPA) {
                total_cpa_mg += amount;
            } else if (is_testosterone_ester(event->ester)) {
                total_t_mg += amount;
            } else {
                total_e2_mg += amount;
            }
        }

        const double conc_e2 = (total_e2_mg * 1e9) / plasma_volume_e2;
        const double conc_cpa = (total_cpa_mg * 1e6) / plasma_volume_cpa;
        const double conc_t = (total_t_mg * 1e8) / plasma_volume_t;

        time_h[i] = point;
        e2[i] = conc_e2;
        cpa[i] = conc_cpa;
        t[i] = conc_t;
        if (i > 0) {
            auc += 0.5 * (conc_e2 + e2[i - 1]) * (point - timeline[i - 1]);
        }
    }

    out->time_h = time_h;
    out->e2 = e2;
    out->cpa = cpa;
    out->t = t;
    out->length = timeline_len;
    out->auc = auc;

    free(indices);
    free(sorted);
    free(removal_times);
    free(precomputed);
    free(timeline);
    free(active);
    return HRTCORE_OK;
}

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
    return interpolate_series(result, result == NULL ? NULL : result->e2, hour);
}

double hrtcore_pk_interp_cpa(const hrtcore_pk_result *result, double hour) {
    return interpolate_series(result, result == NULL ? NULL : result->cpa, hour);
}

double hrtcore_pk_interp_t(const hrtcore_pk_result *result, double hour) {
    return interpolate_series(result, result == NULL ? NULL : result->t, hour);
}
