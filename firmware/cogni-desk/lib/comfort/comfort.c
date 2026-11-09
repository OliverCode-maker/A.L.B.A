#include "comfort.h"

#include <stdio.h>
#include <string.h>

comfort_limits_t comfort_default_limits(void)
{
    comfort_limits_t l;
    l.temp_min_x10 = COMFORT_TEMP_MIN_X10;
    l.temp_max_x10 = COMFORT_TEMP_MAX_X10;
    l.hum_min = (uint8_t)COMFORT_HUM_MIN;
    l.hum_max = (uint8_t)COMFORT_HUM_MAX;
    l.temp_margin_x10 = COMFORT_TEMP_MARGIN_X10;
    l.hum_margin = (uint8_t)COMFORT_HUM_MARGIN;
    l.hold_ms = COMFORT_HOLD_MS;
    l.renotify_ms = COMFORT_RENOTIFY_MS;
    return l;
}

void comfort_init(comfort_t *c, const comfort_limits_t *limits)
{
    if (c == 0) return;
    memset(c, 0, sizeof *c);
    c->lim = limits != 0 ? *limits : comfort_default_limits();
}

uint8_t comfort_classify(const comfort_limits_t *lim, int16_t temp_x10, uint8_t hum_pct)
{
    uint8_t f = 0u;
    if (temp_x10 < lim->temp_min_x10) f |= COMFORT_TEMP_LOW;
    if (temp_x10 > lim->temp_max_x10) f |= COMFORT_TEMP_HIGH;
    if (hum_pct < lim->hum_min) f |= COMFORT_HUM_LOW;
    if (hum_pct > lim->hum_max) f |= COMFORT_HUM_HIGH;
    return f;
}

/* Lo que la lectura pide, con histeresis: una bandera ya activa solo se apaga cuando el valor
 * vuelve al rango con margen. */
static uint8_t desired_flags(const comfort_t *c, int16_t t, uint8_t h)
{
    const comfort_limits_t *l = &c->lim;
    uint8_t f = 0u;
    if (t < l->temp_min_x10 || ((c->active & COMFORT_TEMP_LOW) && t < l->temp_min_x10 + l->temp_margin_x10))
        f |= COMFORT_TEMP_LOW;
    if (t > l->temp_max_x10 || ((c->active & COMFORT_TEMP_HIGH) && t > l->temp_max_x10 - l->temp_margin_x10))
        f |= COMFORT_TEMP_HIGH;
    if (h < l->hum_min || ((c->active & COMFORT_HUM_LOW) && h < (unsigned)l->hum_min + l->hum_margin))
        f |= COMFORT_HUM_LOW;
    if (h > l->hum_max || ((c->active & COMFORT_HUM_HIGH) && h + (unsigned)l->hum_margin > l->hum_max))
        f |= COMFORT_HUM_HIGH;
    return f;
}

void comfort_update(comfort_t *c, uint32_t now_ms, bool valid, int16_t temp_x10, uint8_t hum_pct)
{
    if (c == 0 || !valid) return;

    const uint8_t want = desired_flags(c, temp_x10, hum_pct);
    if (want == c->active) {
        c->candidate = want;
        c->candidate_since_ms = now_ms;
    } else if (want != c->candidate) {
        c->candidate = want;
        c->candidate_since_ms = now_ms;
    } else if ((uint32_t)(now_ms - c->candidate_since_ms) >= c->lim.hold_ms) {
        const uint8_t gained = (uint8_t)(want & (uint8_t)~c->active);
        c->active = want;
        c->change_pending = true;
        if (gained != 0u) c->notify_pending = true;
        if (want == 0u) c->notify_pending = false;
    }

    /* Sigue fuera de rango: recordatorio periodico. */
    if (c->active != 0u && c->notified_once && !c->notify_pending &&
        (uint32_t)(now_ms - c->last_notify_ms) >= c->lim.renotify_ms) {
        c->notify_pending = true;
    }
}

uint8_t comfort_flags(const comfort_t *c)
{
    return c != 0 ? c->active : 0u;
}

bool comfort_take_change(comfort_t *c, uint8_t *flags)
{
    if (c == 0 || !c->change_pending) return false;
    c->change_pending = false;
    if (flags != 0) *flags = c->active;
    return true;
}

bool comfort_notify_pending(const comfort_t *c)
{
    return c != 0 && c->notify_pending && c->active != 0u;
}

void comfort_take_notification(comfort_t *c, uint32_t now_ms)
{
    if (c == 0) return;
    c->notify_pending = false;
    c->notified_once = true;
    c->last_notify_ms = now_ms;
}

int comfort_describe(uint8_t flags, char *out, size_t cap)
{
    if (out == 0 || cap == 0u) return -1;
    out[0] = '\0';
    if (flags == 0u) return snprintf(out, cap, "OK");

    static const struct { uint8_t bit; const char *name; } k_names[] = {
        { COMFORT_TEMP_HIGH, "TEMP_ALTA" }, { COMFORT_TEMP_LOW, "TEMP_BAJA" },
        { COMFORT_HUM_HIGH,  "HUM_ALTA"  }, { COMFORT_HUM_LOW,  "HUM_BAJA"  }
    };
    size_t n = 0u;
    for (unsigned i = 0u; i < sizeof k_names / sizeof k_names[0]; i++) {
        if ((flags & k_names[i].bit) == 0u) continue;
        const int w = snprintf(out + n, cap - n, "%s%s", n != 0u ? "+" : "", k_names[i].name);
        if (w < 0 || (size_t)w >= cap - n) { out[0] = '\0'; return -1; }
        n += (size_t)w;
    }
    return (int)n;
}
