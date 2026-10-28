/**
 * robot_link.h
 * Lado ROBOT del enlace con la pulsera. Recibe lo que llega por ESP-NOW y decide:
 *   - si el paquete es valido (alba_validate) y viene de la pulsera,
 *   - si es NUEVO o un DUPLICADO (la pulsera reenvia una alerta si falla el envio;
 *     un PENDIENTE repetido no debe volver a preguntar "¿Estas bien?"),
 *   - cuantos paquetes se perdieron (huecos en el numero de secuencia),
 *   - si la pulsera se REINICIO (su reloj vuelve a empezar y su `seq` tambien),
 *   - cuando hay que mandarle la HORA (al aparecer, tras un reinicio, y cada 6 h).
 *
 * C puro, sin ESP-NOW ni FreeRTOS: se prueba en el PC. Usarlo desde el callback
 * o la tarea que recibe los paquetes del robot:
 *
 *     robot_rx_msg_t msg;
 *     switch (robot_link_on_packet(&link, buf, len, millis(), &msg)) {
 *     case ROBOT_RX_NEW:        // msg.type dice si es telemetria o evento
 *         ... publicar a ThingsBoard (alba_thingsboard.h), robot_fall_on_event(...) ...
 *         break;
 *     case ROBOT_RX_DUPLICATE:  // ya procesado: ignorar
 *     case ROBOT_RX_INVALID:    // basura: ignorar
 *         break;
 *     }
 *     if (robot_link_need_time(&link, millis())) {   // en la tarea periodica
 *         alba_time_t h; alba_pack_time(&h, seq++, epoch_s_ahora, -300);
 *         esp_now_send(band_mac, &h, sizeof h);
 *         robot_link_time_sent(&link, millis());
 *     }
 *
 * No es thread-safe: usarlo desde una sola tarea.
 */
#ifndef ROBOT_LINK_H
#define ROBOT_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alba_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_LINK_WINDOW        32u                 /* cuantos numeros de secuencia recientes se recuerdan */
#define ROBOT_REBOOT_BACK_MS     10000u              /* si el reloj de la pulsera retrocede mas que esto, se reinicio */
#define ROBOT_TIME_RESYNC_MS     (6ul * 3600ul * 1000ul)   /* reenviar la hora cada 6 h */
#define ROBOT_TIME_BURST         3u                  /* envios seguidos de HORA al aparecer la pulsera */

typedef enum {
    ROBOT_RX_INVALID = 0,   /* no es un paquete valido de la pulsera */
    ROBOT_RX_NEW,           /* paquete valido y no visto antes       */
    ROBOT_RX_DUPLICATE      /* ya lo habiamos recibido               */
} robot_rx_result_t;

/** El paquete ya decodificado (solo se llena el que corresponde a `type`). */
typedef struct {
    int              type;   /* ALBA_MSG_TELEMETRIA o ALBA_MSG_EVENTO */
    alba_telemetry_t tel;
    alba_event_t     evt;
} robot_rx_msg_t;

typedef struct {
    bool     seen_any;
    uint16_t last_seq;           /* el numero de secuencia mas alto recibido        */
    uint32_t window;             /* bit i = ya llego (last_seq - i)                 */
    uint32_t band_t_ms;          /* reloj de la pulsera en el ultimo paquete        */

    uint8_t  time_burst_left;    /* HORA pendientes de enviar en la rafaga inicial  */
    bool     time_sent_once;
    uint32_t time_sent_ms;       /* cuando se mando la ultima HORA                  */

    /* contadores para depurar y para el informe */
    uint32_t rx_new, rx_duplicate, rx_invalid, rx_lost, band_reboots;
} robot_link_t;

void robot_link_init(robot_link_t *l);

robot_rx_result_t robot_link_on_packet(robot_link_t *l, const uint8_t *buf, size_t len,
                                       uint32_t now_ms, robot_rx_msg_t *out);

/** true si toca mandar la HORA a la pulsera. Al llamar a esta funcion no cambia nada:
 *  despues de enviarla, avisar con robot_link_time_sent(). */
bool robot_link_need_time(const robot_link_t *l, uint32_t now_ms);
void robot_link_time_sent(robot_link_t *l, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_LINK_H */
