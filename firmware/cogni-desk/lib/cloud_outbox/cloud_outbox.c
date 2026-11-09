#include "cloud_outbox.h"

#include <stdio.h>
#include <string.h>

#include "alba_thingsboard.h"

static void remove_at(cloud_outbox_t *o, size_t i)
{
    for (size_t k = i; k + 1u < o->count; k++) o->msgs[k] = o->msgs[k + 1u];
    o->count--;
}

static uint32_t push(cloud_outbox_t *o, const char *json, bool is_event)
{
    if (o->count == CLOUD_OUTBOX_CAP) {
        /* Llena: se pierde primero la telemetria mas antigua; si todo son alertas, la mas antigua. */
        size_t victim = 0u;
        for (size_t i = 0u; i < o->count; i++) {
            if (!o->msgs[i].is_event) { victim = i; break; }
        }
        if (o->alert_status == CLOUD_ALERT_PENDING && o->msgs[victim].id == o->alert_id) {
            o->alert_status = CLOUD_ALERT_LOST;
        }
        remove_at(o, victim);
        o->dropped++;
    }
    cloud_msg_t *m = &o->msgs[o->count++];
    m->id = ++o->next_id;
    strncpy(m->json, json, CLOUD_JSON_MAX - 1u);
    m->json[CLOUD_JSON_MAX - 1u] = '\0';
    m->is_event = is_event;
    return m->id;
}

/* Arma {"ts":..,"values":{vals}} o, sin hora, {vals}. */
static void push_values(cloud_outbox_t *o, uint64_t epoch_ms, const char *vals, bool is_event)
{
    char json[CLOUD_JSON_MAX];
    int w;
    if (epoch_ms != 0u) {
        w = snprintf(json, sizeof json, "{\"ts\":%llu,\"values\":{%s}}",
                     (unsigned long long)epoch_ms, vals);
    } else {
        w = snprintf(json, sizeof json, "{%s}", vals);
    }
    if (w > 0 && (size_t)w < sizeof json) push(o, json, is_event);
}

void cloud_outbox_init(cloud_outbox_t *o)
{
    memset(o, 0, sizeof *o);
}

void cloud_outbox_push_telemetry(cloud_outbox_t *o, const alba_telemetry_t *tel, uint64_t epoch_ms)
{
    char json[CLOUD_JSON_MAX];
    const int w = alba_telemetry_to_tb(tel, epoch_ms, json, sizeof json);
    if (w > 0) push(o, json, false);
}

void cloud_outbox_push_event(cloud_outbox_t *o, const alba_event_t *evt, uint64_t epoch_ms)
{
    char json[CLOUD_JSON_MAX];
    const int w = alba_event_to_tb(evt, epoch_ms, json, sizeof json);
    if (w <= 0) return;
    const uint32_t id = push(o, json, true);
    if (evt->alerta_tipo == ALBA_ALERTA_CAIDA_DETECTADA && evt->confirmacion == ALBA_CONF_CONFIRMADA) {
        o->alert_id = id;
        o->alert_status = CLOUD_ALERT_PENDING;
    }
}

void cloud_outbox_push_env(cloud_outbox_t *o, uint64_t epoch_ms, int16_t temp_c_x10, uint8_t hum_pct)
{
    char vals[64];
    size_t n = 0u;
    int w;
    vals[0] = '\0';
    if (temp_c_x10 != CLOUD_ENV_INVALID_TEMP) {
        const int a = temp_c_x10 < 0 ? -(int)temp_c_x10 : (int)temp_c_x10;
        w = snprintf(vals + n, sizeof vals - n, "\"room_temp\":%s%d.%d",
                     temp_c_x10 < 0 ? "-" : "", a / 10, a % 10);
        if (w < 0 || (size_t)w >= sizeof vals - n) return;
        n += (size_t)w;
    }
    if (hum_pct != CLOUD_ENV_INVALID_HUM && hum_pct <= 100u) {
        w = snprintf(vals + n, sizeof vals - n, "%s\"room_hum\":%u", n ? "," : "", (unsigned)hum_pct);
        if (w < 0 || (size_t)w >= sizeof vals - n) return;
        n += (size_t)w;
    }
    if (n == 0u) return;
    push_values(o, epoch_ms, vals, false);
}

void cloud_outbox_push_comfort(cloud_outbox_t *o, uint64_t epoch_ms, const char *text)
{
    char vals[64];
    if (text == NULL || text[0] == '\0') return;
    for (const char *p = text; *p; ++p) {                 /* solo letras, digitos, '_' y '+' */
        const bool ok = (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '+';
        if (!ok) return;
    }
    const int w = snprintf(vals, sizeof vals, "\"room_alert\":\"%s\"", text);
    if (w > 0 && (size_t)w < sizeof vals) push_values(o, epoch_ms, vals, true);
}

void cloud_outbox_push_questions(cloud_outbox_t *o, uint64_t epoch_ms, uint8_t loaded, uint8_t rejected)
{
    char vals[48];
    const int w = snprintf(vals, sizeof vals, "\"q_count\":%u,\"q_rejected\":%u",
                           (unsigned)loaded, (unsigned)rejected);
    if (w > 0 && (size_t)w < sizeof vals) push_values(o, epoch_ms, vals, true);
}

void cloud_outbox_push_game(cloud_outbox_t *o, uint64_t epoch_ms, uint8_t score, uint8_t total,
                            uint32_t avg_ms)
{
    char vals[96];
    const int w = snprintf(vals, sizeof vals,
                           "\"game_score\":%u,\"game_total\":%u,\"game_avg_s\":%lu",
                           (unsigned)score, (unsigned)total, (unsigned long)((avg_ms + 500u) / 1000u));
    if (w > 0 && (size_t)w < sizeof vals) push_values(o, epoch_ms, vals, true);
}

const char *cloud_outbox_peek(const cloud_outbox_t *o)
{
    return o->count ? o->msgs[0].json : NULL;
}

void cloud_outbox_pop(cloud_outbox_t *o)
{
    if (o->count == 0u) return;
    if (o->alert_status == CLOUD_ALERT_PENDING && o->msgs[0].id == o->alert_id) {
        o->alert_status = CLOUD_ALERT_SENT;
    }
    remove_at(o, 0u);
}

cloud_alert_status_t cloud_outbox_alert_status(const cloud_outbox_t *o)
{
    return o->alert_status;
}

cloud_alert_show_t cloud_alert_show(cloud_alert_status_t status, bool cloud_enabled, bool connected)
{
    if (!cloud_enabled) return CLOUD_SHOW_CLOUD_OFF;
    switch (status) {
    case CLOUD_ALERT_PENDING: return connected ? CLOUD_SHOW_SENDING : CLOUD_SHOW_NO_NETWORK;
    case CLOUD_ALERT_SENT:    return CLOUD_SHOW_SENT;
    case CLOUD_ALERT_NONE:
    case CLOUD_ALERT_LOST:
    default:                  return CLOUD_SHOW_NOT_SENT;
    }
}

size_t cloud_outbox_count(const cloud_outbox_t *o)
{
    return o->count;
}
