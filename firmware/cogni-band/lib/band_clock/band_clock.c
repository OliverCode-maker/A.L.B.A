#include "band_clock.h"

#include "alba_protocol.h"
#include "wake_detector.h"     /* WAKE_HOUR_UNKNOWN */

void band_clock_init(band_clock_t *c)
{
    c->synced = false;
    c->epoch_s = 0;
    c->t_ms_at_sync = 0;
    c->utc_offset_min = 0;
}

bool band_clock_sync(band_clock_t *c, uint32_t now_ms, uint32_t epoch_s, int16_t utc_offset_min)
{
    if (epoch_s < ALBA_EPOCH_MIN_S || utc_offset_min < -720 || utc_offset_min > 840) return false;
    c->synced = true;
    c->epoch_s = epoch_s;
    c->t_ms_at_sync = now_ms;
    c->utc_offset_min = utc_offset_min;
    return true;
}

/* Milisegundos desde la sincronizacion; la resta sin signo es correcta aunque
 * el contador de 32 bits se haya desbordado entre medias. */
static uint32_t age_ms(const band_clock_t *c, uint32_t now_ms)
{
    return (uint32_t)(now_ms - c->t_ms_at_sync);
}

uint32_t band_clock_epoch_s(const band_clock_t *c, uint32_t now_ms)
{
    if (!c->synced) return 0;
    const uint32_t age = age_ms(c, now_ms);
    if (age > BAND_CLOCK_MAX_AGE_MS) return 0;
    return c->epoch_s + age / 1000u;
}

uint8_t band_clock_hour(const band_clock_t *c, uint32_t now_ms)
{
    const uint32_t epoch = band_clock_epoch_s(c, now_ms);
    if (epoch == 0) return WAKE_HOUR_UNKNOWN;
    const int64_t local = (int64_t)epoch + (int64_t)c->utc_offset_min * 60;
    /* local es siempre positivo (epoch >= ALBA_EPOCH_MIN_S, desfase <= 14 h). */
    return (uint8_t)((local / 3600) % 24);
}
