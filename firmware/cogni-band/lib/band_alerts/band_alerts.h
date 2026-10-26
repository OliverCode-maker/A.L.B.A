/**
 * band_alerts.h
 * Puente entre los detectores (fall_detector, wake_detector) y el protocolo:
 * traduce "paso esto" a los campos de alba_event_t.
 *
 * Mantenerlo aparte hace que la tarea de comunicaciones sea trivial y que la
 * traduccion se pueda probar en el PC:
 *
 *     band_alert_t a;
 *     if (band_alert_from_fall(evento, fall_detector_features(&fd), &a)) {
 *         alba_event_t pkt;
 *         alba_pack_event(&pkt, seq++, millis(), a.alerta_tipo, a.valor_pico, a.confirmacion);
 *         esp_now_send(robot_mac, (uint8_t *)&pkt, sizeof pkt);
 *     }
 */
#ifndef BAND_ALERTS_H
#define BAND_ALERTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alba_protocol.h"
#include "fall_detector.h"
#include "wake_detector.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Los campos variables de un alba_event_t (el resto lo pone alba_pack_event). */
typedef struct {
    uint8_t  alerta_tipo;   /* alba_alerta_t      */
    uint16_t valor_pico;    /* unidad segun el tipo de alerta (ver alba_protocol.h) */
    uint8_t  confirmacion;  /* alba_confirmacion_t */
} band_alert_t;

/** Traduce un evento del detector de caidas.
 *  CANDIDATE -> PENDIENTE, CONFIRMED -> CONFIRMADA, CANCELLED -> CANCELADA.
 *  NONE y REJECTED no generan mensaje (devuelve false).
 *  valor_pico = pico de aceleracion en mili-g. */
bool band_alert_from_fall(fall_event_t event, const fall_features_t *features, band_alert_t *out);

/** Traduce el evento del detector de vigilia. Solo TRIGGER genera mensaje
 *  (siempre CONFIRMADA). valor_pico = bpm al momento del disparo. */
bool band_alert_from_wake(wake_event_t event, uint8_t bpm, band_alert_t *out);

/* ---- Cancelacion remota de una caida ---------------------------------------
 * El robot envia CANCELAR con el seq del aviso PENDIENTE. La pulsera solo
 * acepta el cancelar si coincide con la caida que esta esperando respuesta;
 * uno tardio, repetido o de otra caida se ignora.
 *
 *   tras enviar un evento con seq N:   band_cancel_note_sent(&gate, &alerta, N);
 *   al recibir CANCELAR (ref_seq R):   if (band_cancel_accept(&gate, R)) fall_detector_cancel(&fd);
 */
typedef struct {
    bool     armed;
    uint16_t seq;
} band_cancel_gate_t;

void band_cancel_gate_init(band_cancel_gate_t *g);

/** Registrar cada alerta enviada: una caida PENDIENTE arma la compuerta; una
 *  CONFIRMADA o CANCELADA la cierra. Otras alertas no la afectan. */
void band_cancel_note_sent(band_cancel_gate_t *g, const band_alert_t *alert, uint16_t seq);

/** true si el CANCELAR corresponde a la caida pendiente (y la cierra). */
bool band_cancel_accept(band_cancel_gate_t *g, uint16_t ref_seq);

#ifdef __cplusplus
}
#endif

#endif /* BAND_ALERTS_H */
