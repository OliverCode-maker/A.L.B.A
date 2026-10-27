/**
 * band_battery.h
 * Convierte el voltaje de la bateria LiPo a porcentaje y decide cuando avisar
 * de bateria baja (una sola vez, con histeresis para no repetir el aviso si el
 * voltaje oscila).
 *
 * El voltaje llega de port_battery_mv(): 0 significa "no se puede medir"
 * (todavia no hay divisor de resistencias en la pulsera), y entonces el
 * porcentaje es ALBA_BAT_INVALID y nunca se avisa.
 *
 * La tabla voltaje -> % es la curva tipica de una LiPo de 1 celda en reposo;
 * con carga el voltaje cae, asi que hay que calibrarla con la bateria real.
 */
#ifndef BAND_BATTERY_H
#define BAND_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BAND_BAT_LOW_PCT     15u   /* avisa al bajar de este nivel            */
#define BAND_BAT_REARM_PCT   25u   /* vuelve a poder avisar al subir de este  */

typedef struct {
    bool low_sent;
} band_battery_t;

void band_battery_init(band_battery_t *b);

/** mv = voltaje de la celda en milivoltios (0 = sin medicion). Devuelve 0-100 o ALBA_BAT_INVALID (0xFF). */
uint8_t band_battery_pct_from_mv(uint16_t mv);

/** true UNA vez cuando el nivel baja de BAND_BAT_LOW_PCT; se rearma al pasar BAND_BAT_REARM_PCT. */
bool band_battery_low_alert(band_battery_t *b, uint8_t pct);

#ifdef __cplusplus
}
#endif

#endif /* BAND_BATTERY_H */
