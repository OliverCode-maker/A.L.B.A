#include "robot_link.h"

#include <string.h>

/* La HORA se manda en rafaga porque ESP-NOW no confirma la recepcion: al
 * aparecer la pulsera (o reiniciarse) se envia de inmediato, otra vez a los
 * 5 s y otra a los 30 s. Despues, cada ROBOT_TIME_RESYNC_MS. */
static const uint32_t k_burst_gap_ms[ROBOT_TIME_BURST] = { 0u, 5000u, 30000u };

void robot_link_init(robot_link_t *l)
{
    memset(l, 0, sizeof *l);
}

static void restart_window(robot_link_t *l, uint16_t seq)
{
    l->seen_any = true;
    l->last_seq = seq;
    l->window   = 1u;
}

static void start_time_burst(robot_link_t *l)
{
    l->time_burst_left = ROBOT_TIME_BURST;
    l->time_sent_once  = false;
}

robot_rx_result_t robot_link_on_packet(robot_link_t *l, const uint8_t *buf, size_t len,
                                       uint32_t now_ms, robot_rx_msg_t *out)
{
    (void)now_ms;
    const int type = alba_validate(buf, len);
    /* HORA y CANCELAR viajan robot -> pulsera: si alguien nos los manda, no son para nosotros. */
    if (type != ALBA_MSG_TELEMETRIA && type != ALBA_MSG_EVENTO) {
        l->rx_invalid++;
        return ROBOT_RX_INVALID;
    }

    robot_rx_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = type;
    uint16_t seq;
    uint32_t t_ms;
    if (type == ALBA_MSG_TELEMETRIA) {
        memcpy(&m.tel, buf, sizeof m.tel);          /* el buffer puede venir desalineado */
        seq = m.tel.hdr.seq;  t_ms = m.tel.timestamp_ms;
    } else {
        memcpy(&m.evt, buf, sizeof m.evt);
        seq = m.evt.hdr.seq;  t_ms = m.evt.timestamp_ms;
    }

    /* 1) Primer paquete, o la pulsera se reinicio (su reloj retrocede de golpe). */
    const bool rebooted = l->seen_any &&
                          ((int32_t)(t_ms - l->band_t_ms) < -(int32_t)ROBOT_REBOOT_BACK_MS);
    if (!l->seen_any || rebooted) {
        if (rebooted) l->band_reboots++;
        restart_window(l, seq);
        l->band_t_ms = t_ms;
        start_time_burst(l);                         /* una pulsera nueva no sabe la hora */
        l->rx_new++;
        if (out) *out = m;
        return ROBOT_RX_NEW;
    }

    /* 2) Numero de secuencia respecto al mas alto visto (aritmetica circular de 16 bits). */
    const int16_t diff = (int16_t)(uint16_t)(seq - l->last_seq);

    if (diff == 0) {
        l->rx_duplicate++;
        return ROBOT_RX_DUPLICATE;
    }
    if (diff > 0) {                                  /* llego uno mas nuevo */
        const uint16_t gap = (uint16_t)(diff - 1);
        l->rx_lost += gap;                           /* los que se saltaron: perdidos (por ahora) */
        l->window = (diff >= (int16_t)ROBOT_LINK_WINDOW) ? 0u : (l->window << diff);
        l->window |= 1u;
        l->last_seq = seq;
    } else {                                         /* llego uno viejo (desordenado o repetido) */
        const unsigned age = (unsigned)(-(int)diff);
        if (age >= ROBOT_LINK_WINDOW) {              /* demasiado viejo para saberlo: tratarlo como repetido */
            l->rx_duplicate++;
            return ROBOT_RX_DUPLICATE;
        }
        const uint32_t bit = 1u << age;
        if (l->window & bit) {
            l->rx_duplicate++;
            return ROBOT_RX_DUPLICATE;
        }
        l->window |= bit;
        if (l->rx_lost > 0u) l->rx_lost--;           /* no estaba perdido: solo llego tarde */
    }

    if ((int32_t)(t_ms - l->band_t_ms) > 0) l->band_t_ms = t_ms;
    l->rx_new++;
    if (out) *out = m;
    return ROBOT_RX_NEW;
}

bool robot_link_need_time(const robot_link_t *l, uint32_t now_ms)
{
    if (!l->seen_any) return false;                  /* sin pulsera a la vista no hay a quien mandarle la hora */
    if (l->time_burst_left > 0u) {
        const unsigned idx = ROBOT_TIME_BURST - l->time_burst_left;   /* 0, 1, 2 */
        if (!l->time_sent_once) return true;
        return (uint32_t)(now_ms - l->time_sent_ms) >= k_burst_gap_ms[idx];
    }
    return (uint32_t)(now_ms - l->time_sent_ms) >= ROBOT_TIME_RESYNC_MS;
}

void robot_link_time_sent(robot_link_t *l, uint32_t now_ms)
{
    l->time_sent_ms   = now_ms;
    l->time_sent_once = true;
    if (l->time_burst_left > 0u) l->time_burst_left--;
}
