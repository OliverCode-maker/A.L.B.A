/**
 * comfort.h
 * Alerta de ambiente fuera del rango de confort (logica pura, sin hardware).
 *
 * La propuesta del proyecto pide que el robot avise si la temperatura o la humedad de la
 * habitacion se salen del rango de confort, para prevenir la agitacion o el estres termico.
 * Rangos por defecto (los de "Condiciones de operacion" de la propuesta):
 *   temperatura 15.0 a 35.0 C, humedad relativa 20 a 80 %.
 *
 * Para no avisar por una lectura suelta del DHT11:
 *   - la condicion debe mantenerse COMFORT_HOLD_MS (30 s) antes de dar la alerta;
 *   - para volver a "normal" el valor debe regresar con un margen (1 C / 3 %): histeresis;
 *   - si la alerta sigue activa, se vuelve a avisar cada COMFORT_RENOTIFY_MS (30 min).
 *
 *     comfort_update(&c, now_ms, ambiente.valid, temp_x10, hum);
 *     if (comfort_take_change(&c, &flags)) publicar_a_la_nube(flags);
 *     if (comfort_notify_pending(&c) && pantalla_acepta_alerta()) {
 *         comfort_take_notification(&c, now_ms);
 *     }
 */
#ifndef COMFORT_H
#define COMFORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMFORT_TEMP_LOW   0x01u
#define COMFORT_TEMP_HIGH  0x02u
#define COMFORT_HUM_LOW    0x04u
#define COMFORT_HUM_HIGH   0x08u

#define COMFORT_TEMP_MIN_X10   150   /* 15.0 C */
#define COMFORT_TEMP_MAX_X10   350   /* 35.0 C */
#define COMFORT_HUM_MIN        20u
#define COMFORT_HUM_MAX        80u
#define COMFORT_TEMP_MARGIN_X10 10   /* 1.0 C de histeresis */
#define COMFORT_HUM_MARGIN     3u
#define COMFORT_HOLD_MS        30000ul
#define COMFORT_RENOTIFY_MS    (30ul * 60ul * 1000ul)

typedef struct {
    int16_t  temp_min_x10, temp_max_x10;
    uint8_t  hum_min, hum_max;
    int16_t  temp_margin_x10;
    uint8_t  hum_margin;
    uint32_t hold_ms;
    uint32_t renotify_ms;
} comfort_limits_t;

typedef struct {
    comfort_limits_t lim;
    uint8_t  active;            /* banderas COMFORT_* de la alerta vigente */
    uint8_t  candidate;         /* lo que las lecturas piden y aun no cumple hold_ms */
    uint32_t candidate_since_ms;
    bool     change_pending;    /* active cambio y falta publicarlo */
    bool     notify_pending;    /* falta avisar en pantalla/buzzer */
    bool     notified_once;
    uint32_t last_notify_ms;
} comfort_t;

comfort_limits_t comfort_default_limits(void);
void comfort_init(comfort_t *c, const comfort_limits_t *limits);   /* NULL = rangos por defecto */

/* Sin histeresis: que banderas dispararia esta lectura. Sirve para mostrar "fuera de rango". */
uint8_t comfort_classify(const comfort_limits_t *lim, int16_t temp_x10, uint8_t hum_pct);

/* Una lectura nueva. Las lecturas no validas se ignoran. */
void comfort_update(comfort_t *c, uint32_t now_ms, bool valid, int16_t temp_x10, uint8_t hum_pct);

uint8_t comfort_flags(const comfort_t *c);
/* true una vez cada vez que cambia el conjunto de banderas activas (incluye volver a normal, flags = 0). */
bool comfort_take_change(comfort_t *c, uint8_t *flags);
/* Falta avisar en pantalla? (nueva alerta, o recordatorio si sigue activa). */
bool comfort_notify_pending(const comfort_t *c);
/* Llamar cuando la pantalla ya mostro el aviso. */
void comfort_take_notification(comfort_t *c, uint32_t now_ms);

/* Texto corto para la nube: "OK", "TEMP_ALTA", "HUM_BAJA", "TEMP_ALTA+HUM_BAJA"... */
int comfort_describe(uint8_t flags, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
