/**
 * band_channel.h
 * Busqueda automatica del canal Wi-Fi/ESP-NOW de la pulsera. Logica pura (sin hardware).
 *
 * Problema: el robot se conecta al router (o al hotspot del celular) y su radio pasa al canal
 * del router. Si la pulsera sigue en otro canal, el robot no la oye. La pulsera no sabe cual
 * es el canal, asi que lo busca: cuando el envio unicast falla (sin ACK del robot) varias
 * veces seguidas, prueba cada canal hasta que uno recibe ACK.
 *
 *   despues de cada envio:   band_channel_note_result(&c, hubo_ack);
 *   si band_channel_should_hunt(&c):
 *       band_channel_hunt_begin(&c);
 *       while ((ch = band_channel_hunt_next(&c)) != 0) {
 *           poner_canal(ch);
 *           if (enviar_con_ack()) { band_channel_hunt_found(&c, ch); break; }
 *       }
 *       // si no hubo suerte, volver a band_channel_current(&c)
 */
#ifndef BAND_CHANNEL_H
#define BAND_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BAND_CH_MIN            1u
#define BAND_CH_MAX            13u
#define BAND_CH_FAILS_TO_HUNT  2u   /* fallos seguidos antes de buscar */

typedef struct {
    uint8_t  current;       /* canal en uso */
    uint8_t  fails;         /* envios seguidos sin ACK */
    uint8_t  scan_pos;      /* siguiente posicion de la lista de busqueda */
    bool     hunting;
    uint32_t ok_total;
    uint32_t fail_total;
    uint32_t hunts_total;
    uint32_t changes_total; /* veces que se cambio de canal */
} band_channel_t;

void    band_channel_init(band_channel_t *c, uint8_t start_channel);
void    band_channel_note_result(band_channel_t *c, bool acked);
bool    band_channel_should_hunt(const band_channel_t *c);
void    band_channel_hunt_begin(band_channel_t *c);
/* Siguiente canal a probar (1..13), o 0 cuando ya se probaron todos. Nunca devuelve el actual. */
uint8_t band_channel_hunt_next(band_channel_t *c);
void    band_channel_hunt_found(band_channel_t *c, uint8_t channel);
uint8_t band_channel_current(const band_channel_t *c);

#ifdef __cplusplus
}
#endif

#endif
