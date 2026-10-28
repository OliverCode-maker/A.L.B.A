# Integracion del robot (cogni-desk) con la pulsera y ThingsBoard

Para Oliver y Gaby. Todo lo de abajo ya existe y esta probado en el PC; **no toca `src/main.cpp`**:
solo hay que llamarlo. Los modulos son C puro (sin ESP-NOW, sin WiFi, sin FreeRTOS), asi que el
robot decide como conectarlos.

| Quiero... | Uso | Donde |
|-----------|-----|-------|
| validar y decodificar un paquete de la pulsera | `robot_link_on_packet()` | `lib/robot_bridge/robot_link.h` |
| ignorar reenvios de la misma alerta | (lo hace `robot_link`: devuelve `ROBOT_RX_DUPLICATE`) | idem |
| saber cuando mandar la HORA a la pulsera | `robot_link_need_time()` / `robot_link_time_sent()` | idem |
| preguntar "¿Estas bien?" y cancelar | `robot_fall_on_event()`, `robot_fall_on_button_ok()`, `robot_fall_tick()` | `lib/robot_bridge/robot_fall.h` |
| armar HORA y CANCELAR | `alba_pack_time()`, `alba_pack_cancel()` | `../common/alba_codec.h` |
| publicar a ThingsBoard | `alba_telemetry_to_tb()`, `alba_event_to_tb()` | `../common/alba_thingsboard.h` |

## Flujo completo

```
 pulsera --ESP-NOW--> robot_link_on_packet --NEW--> telemetria: alba_telemetry_to_tb -> MQTT
                                                |-> evento:     alba_event_to_tb     -> MQTT
                                                '-> evento de caida: robot_fall_on_event
 robot_fall:  SHOW_PROMPT -> pantalla/voz "¿Estas bien?"   | boton -> robot_fall_on_button_ok
              SEND_CANCEL -> alba_pack_cancel(seq++, ref_seq) -> esp_now_send   (se repite solo, ver abajo)
              SHOW_HELP_CALLED -> "ya avise a tu cuidador"   | HIDE_PROMPT -> quitar la pregunta
```

## Esqueleto de uso (pseudocodigo; los nombres de ESP-NOW/MQTT son los del robot)

```cpp
#include "alba_thingsboard.h"
#include "robot_link.h"
#include "robot_fall.h"

static robot_link_t link;      // init una vez: robot_link_init(&link);
static robot_fall_t fall;      //               robot_fall_init(&fall);
static uint16_t robot_seq;     // contador de lo que el robot ENVIA

// --- 1) cada paquete que llega por ESP-NOW (en la tarea receptora, no en el callback de la radio) ---
void on_band_packet(const uint8_t *buf, size_t len) {
    robot_rx_msg_t m;
    if (robot_link_on_packet(&link, buf, len, millis(), &m) != ROBOT_RX_NEW) return;   // basura o repetido

    char json[200];
    uint64_t now_ms = epoch_ms_ahora();                 // hora real (NTP) en milisegundos Unix
    if (m.type == ALBA_MSG_TELEMETRIA) {
        if (alba_telemetry_to_tb(&m.tel, now_ms, json, sizeof json) > 0) mqtt_publish(ALBA_TB_TOPIC, json);
    } else {
        if (alba_event_to_tb(&m.evt, now_ms, json, sizeof json) > 0)     mqtt_publish(ALBA_TB_TOPIC, json);
        uint16_t ref;
        handle(robot_fall_on_event(&fall, &m.evt, millis(), &ref), ref);
    }
}

// --- 2) cuando el paciente pulsa el boton "estoy bien" ---
void on_button_ok() { uint16_t ref; handle(robot_fall_on_button_ok(&fall, millis(), &ref), ref); }

// --- 3) cada ~100 ms ---
void periodic() {
    uint16_t ref;
    handle(robot_fall_tick(&fall, millis(), &ref), ref);       // reenvia CANCELAR y vence la pregunta
    if (robot_link_need_time(&link, millis())) {
        alba_time_t h;  alba_pack_time(&h, robot_seq++, epoch_s_ahora(), -300);   // -300 = Ecuador (UTC-5)
        esp_now_send(band_mac, (uint8_t *)&h, sizeof h);
        robot_link_time_sent(&link, millis());
    }
}

void handle(robot_action_t a, uint16_t ref) {
    switch (a) {
    case ROBOT_ACT_SHOW_PROMPT:      ui_show_are_you_ok();  break;
    case ROBOT_ACT_HIDE_PROMPT:      ui_hide_prompt();      break;
    case ROBOT_ACT_SHOW_HELP_CALLED: ui_show_help_called(); break;
    case ROBOT_ACT_SEND_CANCEL: { alba_cancel_t c; alba_pack_cancel(&c, robot_seq++, ref);
                                  esp_now_send(band_mac, (uint8_t *)&c, sizeof c); } break;
    default: break;
    }
}
```

## Cosas que el robot debe cuidar

1. **Hora real.** `epoch_ms_ahora()` viene de NTP (el robot tiene WiFi). Sin hora, pasar `0` a `alba_*_to_tb`:
   se omite `ts` y ThingsBoard usa la hora de su servidor. A la pulsera solo se le manda la HORA si hay una real
   (`ALBA_EPOCH_MIN_S`); un reloj en 1970 es rechazado por ella.
2. **ESP-NOW no confirma.** Por eso `robot_link` descarta duplicados (la pulsera reintenta alertas) y por eso
   `robot_fall` repite el CANCELAR 3 veces mas, cada 400 ms; la pulsera ignora los repetidos sin problema.
3. **La HORA se manda en rafaga** (al aparecer la pulsera, a los 5 s y a los 30 s) y luego cada 6 h. Si la pulsera
   se reinicia, `robot_link` lo detecta solo (su reloj retrocede) y vuelve a mandarla.
4. **Canal WiFi.** ESP-NOW exige que pulsera y robot usen el mismo canal; si el robot se conecta a un router,
   queda en el canal del router. Hay que fijarlo o ponerlo igual en la pulsera (pendiente de definir).
5. **No usar `robot_link` / `robot_fall` desde dos tareas a la vez**: no son thread-safe.
6. **Credenciales** (token de ThingsBoard, clave del WiFi): nunca en el repositorio (es publico).

## Pruebas

En el PC, sin placa:
- `cd firmware/cogni-desk && pio test -e native` (32 pruebas del enlace y del asistente de caidas).
- `cd firmware/cogni-band && pio test -e native -f test_robot_roundtrip` (pulsera y robot juntos: caida, cancelar,
  cancelar perdido en el aire, duplicados, hora).
