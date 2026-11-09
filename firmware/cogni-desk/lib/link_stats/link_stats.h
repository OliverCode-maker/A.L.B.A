/**
 * link_stats.h
 * Cuenta paquetes recibidos y perdidos de la pulsera usando el contador `seq` del protocolo
 * (un solo contador para todo lo que sale de la pulsera). Logica pura, sin hardware.
 *
 * Un salto de seq = paquetes perdidos. Un salto enorme o hacia atras se toma como reinicio de
 * la pulsera (su contador vuelve a 0) y no cuenta como perdida.
 */
#ifndef LINK_STATS_H
#define LINK_STATS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_STATS_MAX_GAP 200u   /* huecos mayores se consideran reinicio de la pulsera */

typedef struct {
    bool     have_last;
    uint16_t last_seq;
    uint32_t received;
    uint32_t lost;
    uint32_t resets;
} link_stats_t;

void     link_stats_init(link_stats_t *s);
void     link_stats_on_seq(link_stats_t *s, uint16_t seq);
/* Porcentaje de perdida 0..100 (0 si aun no hay datos). */
uint8_t  link_stats_loss_pct(const link_stats_t *s);

#ifdef __cplusplus
}
#endif

#endif
