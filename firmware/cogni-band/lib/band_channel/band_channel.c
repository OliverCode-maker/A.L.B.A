#include "band_channel.h"

/* Primero los canales que usan casi todos los routers y hotspots (1, 6, 11), luego el resto. */
static const uint8_t k_order[BAND_CH_MAX] = {1u, 6u, 11u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 10u, 12u, 13u};

void band_channel_init(band_channel_t *c, uint8_t start_channel)
{
    if (c == 0) return;
    c->current = (start_channel >= BAND_CH_MIN && start_channel <= BAND_CH_MAX) ? start_channel : BAND_CH_MIN;
    c->fails = 0u;
    c->scan_pos = 0u;
    c->hunting = false;
    c->ok_total = 0u;
    c->fail_total = 0u;
    c->hunts_total = 0u;
    c->changes_total = 0u;
}

void band_channel_note_result(band_channel_t *c, bool acked)
{
    if (c == 0) return;
    if (acked) {
        c->fails = 0u;
        c->ok_total++;
    } else {
        if (c->fails < 255u) c->fails++;
        c->fail_total++;
    }
}

bool band_channel_should_hunt(const band_channel_t *c)
{
    return c != 0 && !c->hunting && c->fails >= BAND_CH_FAILS_TO_HUNT;
}

void band_channel_hunt_begin(band_channel_t *c)
{
    if (c == 0) return;
    c->hunting = true;
    c->scan_pos = 0u;
    c->hunts_total++;
}

uint8_t band_channel_hunt_next(band_channel_t *c)
{
    if (c == 0 || !c->hunting) return 0u;
    while (c->scan_pos < BAND_CH_MAX) {
        const uint8_t ch = k_order[c->scan_pos++];
        if (ch != c->current) return ch;
    }
    c->hunting = false;
    c->fails = 0u;   /* no se encontro: se vuelve a intentar tras otros fallos seguidos */
    return 0u;
}

void band_channel_hunt_found(band_channel_t *c, uint8_t channel)
{
    if (c == 0) return;
    if (channel != c->current) c->changes_total++;
    if (channel >= BAND_CH_MIN && channel <= BAND_CH_MAX) c->current = channel;
    c->hunting = false;
    c->fails = 0u;
    c->ok_total++;
}

uint8_t band_channel_current(const band_channel_t *c)
{
    return c != 0 ? c->current : 0u;
}
