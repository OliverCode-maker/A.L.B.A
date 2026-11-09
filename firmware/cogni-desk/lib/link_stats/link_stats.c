#include "link_stats.h"

void link_stats_init(link_stats_t *s)
{
    if (s == 0) return;
    s->have_last = false;
    s->last_seq = 0u;
    s->received = 0u;
    s->lost = 0u;
    s->resets = 0u;
}

void link_stats_on_seq(link_stats_t *s, uint16_t seq)
{
    if (s == 0) return;
    if (!s->have_last) {
        s->have_last = true;
    } else {
        const uint16_t step = (uint16_t)(seq - s->last_seq);   /* 1 = consecutivo */
        if (step == 0u) return;                                  /* repetido */
        if (step == 1u) {
            /* sin perdida */
        } else if (step <= LINK_STATS_MAX_GAP) {
            s->lost += (uint32_t)(step - 1u);
        } else {
            s->resets++;   /* hacia atras o salto enorme: la pulsera se reinicio */
        }
    }
    s->last_seq = seq;
    s->received++;
}

uint8_t link_stats_loss_pct(const link_stats_t *s)
{
    if (s == 0) return 0u;
    const uint32_t total = s->received + s->lost;
    if (total == 0u) return 0u;
    return (uint8_t)((s->lost * 100u + total / 2u) / total);
}
