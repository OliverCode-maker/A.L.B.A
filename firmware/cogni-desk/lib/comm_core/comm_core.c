#include "comm_core.h"

#include <stdio.h>
#include <string.h>

#include "alba_thingsboard.h"

/* ---- cola de salida --------------------------------------------------------- */

static void outbox_remove_at(comm_core_t *c, size_t i)
{
    for (size_t k = i; k + 1u < c->out_count; k++) c->outbox[k] = c->outbox[k + 1u];
    c->out_count--;
}

static void outbox_push(comm_core_t *c, const char *json, bool is_event)
{
    if (c->out_count == COMM_OUTBOX_CAP) {
        /* Llena: se pierde primero la telemetria mas antigua; solo si todo son
         * alertas se pierde la alerta mas antigua. */
        size_t victim = 0u;
        for (size_t i = 0; i < c->out_count; i++) {
            if (!c->outbox[i].is_event) { victim = i; break; }
        }
        outbox_remove_at(c, victim);
        c->out_dropped++;
    }
    comm_msg_t *m = &c->outbox[c->out_count++];
    strncpy(m->json, json, COMM_JSON_MAX - 1u);
    m->json[COMM_JSON_MAX - 1u] = '\0';
    m->is_event = is_event;
}

bool comm_core_push_raw(comm_core_t *c, const char *json, bool is_event)
{
    if (json == NULL || json[0] != '{' || strlen(json) >= COMM_JSON_MAX) return false;
    outbox_push(c, json, is_event);
    return true;
}

const char *comm_core_outbox_peek(const comm_core_t *c)
{
    return c->out_count ? c->outbox[0].json : NULL;
}

void comm_core_outbox_pop(comm_core_t *c)
{
    if (c->out_count) outbox_remove_at(c, 0u);
}

size_t comm_core_outbox_count(const comm_core_t *c) { return c->out_count; }

/* ---- envio a la pulsera ------------------------------------------------------ */

static void send_to_band(comm_core_t *c, const void *pkt, size_t len)
{
    if (c->send == NULL || !c->send((const uint8_t *)pkt, len, c->send_ctx)) c->send_fail++;
}

static void apply_action(comm_core_t *c, robot_action_t a, uint16_t ref_seq)
{
    switch (a) {
    case ROBOT_ACT_SHOW_PROMPT:
        c->prompt_active = true;
        break;
    case ROBOT_ACT_HIDE_PROMPT:
        c->prompt_active = false;
        break;
    case ROBOT_ACT_SHOW_HELP_CALLED:
        c->prompt_active = false;
        c->help_called = true;
        break;
    case ROBOT_ACT_SEND_CANCEL: {
        alba_cancel_t m;
        alba_pack_cancel(&m, c->tx_seq++, ref_seq);
        send_to_band(c, &m, sizeof m);
        c->prompt_active = false;
        break;
    }
    case ROBOT_ACT_NONE:
    default:
        break;
    }
}

/* ---- API ----------------------------------------------------------------------- */

void comm_core_init(comm_core_t *c, comm_send_fn send, void *send_ctx)
{
    memset(c, 0, sizeof *c);
    robot_link_init(&c->link);
    robot_fall_init(&c->fall);
    c->send = send;
    c->send_ctx = send_ctx;
}

void comm_core_on_packet(comm_core_t *c, const uint8_t *buf, size_t len,
                         uint32_t now_ms, uint64_t epoch_ms)
{
    robot_rx_msg_t m;
    if (robot_link_on_packet(&c->link, buf, len, now_ms, &m) != ROBOT_RX_NEW) return;

    c->seen_band = true;
    c->last_rx_ms = now_ms;

    char json[COMM_JSON_MAX];
    if (m.type == ALBA_MSG_TELEMETRIA) {
        c->have_vitals = true;
        c->bpm = m.tel.bpm;
        c->spo2 = m.tel.spo2;
        c->bat = m.tel.bateria_pct;
        c->vitals_ms = now_ms;
        if (alba_telemetry_to_tb(&m.tel, epoch_ms, json, sizeof json) > 0) outbox_push(c, json, false);
    } else if (m.type == ALBA_MSG_EVENTO) {
        if (alba_event_to_tb(&m.evt, epoch_ms, json, sizeof json) > 0) outbox_push(c, json, true);
        uint16_t ref = 0;
        const robot_action_t a = robot_fall_on_event(&c->fall, &m.evt, now_ms, &ref);
        apply_action(c, a, ref);
    }
}

void comm_core_button_ok(comm_core_t *c, uint32_t now_ms)
{
    uint16_t ref = 0;
    const robot_action_t a = robot_fall_on_button_ok(&c->fall, now_ms, &ref);
    apply_action(c, a, ref);
}

void comm_core_tick(comm_core_t *c, uint32_t now_ms, uint32_t epoch_s, int16_t utc_offset_min)
{
    uint16_t ref = 0;
    apply_action(c, robot_fall_tick(&c->fall, now_ms, &ref), ref);

    if (epoch_s >= ALBA_EPOCH_MIN_S && robot_link_need_time(&c->link, now_ms)) {
        alba_time_t h;
        alba_pack_time(&h, c->tx_seq++, epoch_s, utc_offset_min);
        send_to_band(c, &h, sizeof h);
        robot_link_time_sent(&c->link, now_ms);   /* se marca aunque falle: reintenta por la rafaga/6 h */
    }
}

void comm_core_push_env(comm_core_t *c, uint64_t epoch_ms, int16_t temp_c_x10, uint8_t hum_pct)
{
    char vals[64];
    size_t n = 0;
    vals[0] = '\0';
    int w;
    if (temp_c_x10 != COMM_ENV_INVALID_TEMP) {
        const int16_t a = (int16_t)(temp_c_x10 < 0 ? -temp_c_x10 : temp_c_x10);
        w = snprintf(vals + n, sizeof vals - n, "\"room_temp\":%s%d.%d",
                     temp_c_x10 < 0 ? "-" : "", a / 10, a % 10);
        if (w < 0 || (size_t)w >= sizeof vals - n) return;
        n += (size_t)w;
    }
    if (hum_pct != COMM_ENV_INVALID_HUM && hum_pct <= 100u) {
        w = snprintf(vals + n, sizeof vals - n, "%s\"room_hum\":%u", n ? "," : "", (unsigned)hum_pct);
        if (w < 0 || (size_t)w >= sizeof vals - n) return;
        n += (size_t)w;
    }
    if (n == 0u) return;

    char json[COMM_JSON_MAX];
    char ts_s[40];
    ts_s[0] = '\0';
    if (epoch_ms != 0u) snprintf(ts_s, sizeof ts_s, "\"ts\":%llu,", (unsigned long long)epoch_ms);
    w = snprintf(json, sizeof json, "{%s\"values\":{%s}}", ts_s, vals);
    if (w > 0 && (size_t)w < sizeof json) outbox_push(c, json, false);
}

bool comm_core_prompt_active(const comm_core_t *c) { return c->prompt_active; }

bool comm_core_take_help_called(comm_core_t *c)
{
    const bool v = c->help_called;
    c->help_called = false;
    return v;
}

bool comm_core_band_online(const comm_core_t *c, uint32_t now_ms)
{
    return c->seen_band && (uint32_t)(now_ms - c->last_rx_ms) < COMM_BAND_OFFLINE_MS;
}
