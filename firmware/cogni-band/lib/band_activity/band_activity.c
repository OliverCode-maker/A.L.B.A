#include "band_activity.h"

void band_activity_init(band_activity_t *a)
{
    a->sum_pct = 0;
    a->n = 0;
}

void band_activity_add(band_activity_t *a, uint8_t pct)
{
    if (pct > 100u) pct = 100u;
    a->sum_pct += pct;
    a->n++;
}

uint8_t band_activity_take(band_activity_t *a)
{
    uint8_t avg = 0;
    if (a->n > 0u) avg = (uint8_t)((a->sum_pct + a->n / 2u) / a->n);
    a->sum_pct = 0;
    a->n = 0;
    return avg;
}
