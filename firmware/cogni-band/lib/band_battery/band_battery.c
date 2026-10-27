#include "band_battery.h"

#include "alba_protocol.h"   /* ALBA_BAT_INVALID */

typedef struct { uint16_t mv; uint8_t pct; } pt_t;

/* Curva tipica LiPo 1S en reposo. */
static const pt_t k_curve[] = {
    { 3300,   0 }, { 3500,   5 }, { 3600,  15 }, { 3700,  35 }, { 3800,  55 },
    { 3900,  70 }, { 4000,  82 }, { 4100,  92 }, { 4200, 100 }
};
#define N_PTS (sizeof k_curve / sizeof k_curve[0])

void band_battery_init(band_battery_t *b)
{
    b->low_sent = false;
}

uint8_t band_battery_pct_from_mv(uint16_t mv)
{
    if (mv == 0u) return ALBA_BAT_INVALID;
    if (mv <= k_curve[0].mv) return 0u;
    if (mv >= k_curve[N_PTS - 1u].mv) return 100u;
    for (unsigned i = 1; i < N_PTS; i++) {
        if (mv <= k_curve[i].mv) {
            const uint32_t dmv  = (uint32_t)(k_curve[i].mv  - k_curve[i - 1u].mv);
            const uint32_t dpct = (uint32_t)(k_curve[i].pct - k_curve[i - 1u].pct);
            const uint32_t off  = (uint32_t)(mv - k_curve[i - 1u].mv);
            return (uint8_t)(k_curve[i - 1u].pct + (off * dpct + dmv / 2u) / dmv);
        }
    }
    return 100u;   /* inalcanzable */
}

bool band_battery_low_alert(band_battery_t *b, uint8_t pct)
{
    if (pct == ALBA_BAT_INVALID) return false;
    if (pct >= BAND_BAT_REARM_PCT) { b->low_sent = false; return false; }
    if (pct <= BAND_BAT_LOW_PCT && !b->low_sent) { b->low_sent = true; return true; }
    return false;
}
