# Integracion del robot (cogni-desk) con la pulsera y ThingsBoard

Para Oliver y Gaby. El firmware del escritorio separa la maquina de estados de la aplicacion
(C puro, `src/DeskApp.c`) de los adaptadores Arduino de pantalla, botones y ESP-NOW (C++).
`src/main.cpp` solo coordina esas capas. El enlace ESP-NOW recibe y valida paquetes de la pulsera,
y permite devolver CANCELAR. La publicacion a la nube por MQTT sigue pendiente.

El prototipo actual cubre el splash, ojos con parpadeo no bloqueante, menu de cuatro opciones
(Sensor, Salud, CogniGame y Reloj), la vista del DHT11 y las interfaces de caida y recordatorio
de medicamento. El reloj sincroniza hora local por NTP cuando hay Wi-Fi y ofrece tres alarmas
diarias persistentes. Salud y CogniGame siguen pendientes de conectar a sus funciones.
No se debe mostrar que se aviso al cuidador hasta completar y verificar la publicacion en la nube.

Las cuatro opciones del menu usan sus iconos bitmap. Las alertas de caida tienen prioridad sobre
la pantalla de medicamentos.
Las alarmas se configuran desde Reloj y se guardan en NVS; confirmar el aviso con OK solo lo
cierra y no registra una toma de medicamento.

El mapa de pines confirmado esta centralizado en `include/DeskConfig.h`: botones GPIO 4/5/6,
DHT11 GPIO 7 y dos buses I2C OLED en GPIO 8/9 y 17/18. El DHT11 se consulta cada 2 segundos al
abrir "Sensor ambiente"; las lecturas invalidas se muestran como "Sin lectura" y se informan
una sola vez por fallo continuo.

El enlace ESP-NOW del escritorio usa canal 1 y filtra la MAC STA configurada de la pulsera
(`AC:27:6E:CF:88:E4`). La MAC STA del escritorio debe coincidir con `44:1B:F6:8C:AC:04`, que
la pulsera tiene configurada como peer. El escritorio imprime su MAC al arrancar e informa si
no coincide. La interfaz y el DHT11 siguen funcionando aunque ESP-NOW no este disponible; solo
se desactiva la comunicacion. Si el modulo ESP32-S3 usado tiene otra MAC, actualiza
`DESK_EXPECTED_STA_MAC` y `BAND_ESPNOW_PEER_MAC` para que ambas configuraciones coincidan.

El callback de radio solo copia paquetes de la pulsera a una cola; el loop valida y deduplica
con `robot_link` antes de cambiar la pantalla. La accion CANCELAR se envia por ESP-NOW y se
reintenta segun `robot_fall`. MQTT aun no esta integrado; una caida confirmada se muestra como
pendiente de aviso al cuidador. Al integrar MQTT, la conexion Wi-Fi debe permanecer en canal 1
para compartir la radio con ESP-NOW.

## Salud y buzzer

La opcion **Salud** del menu muestra pulso, SpO2 y bateria de la ultima telemetria recibida de la
pulsera (`leerSalud()` en `CommManager`). Si pasan `DESK_HEALTH_STALE_MS` (90 s) sin telemetria se
muestra "Sin senal de la pulsera". Un valor "--" significa que la pulsera lo mando como invalido
(por ejemplo, el MAX30102 no detecta la muneca, o la pulsera no mide su bateria).

El buzzer esta en el GPIO 14 (`DESK_BUZZER_GPIO`) y se maneja con `tone()`, asi que sirve activo o
pasivo. Suena con tres patrones segun la pantalla: pitidos rapidos en "posible caida", largos en
"caida confirmada" (ambos hasta pulsar OK), y suaves en "medicamento" (maximo 60 s).

## CogniGame

La opcion **CogniGame** juega rondas de 5 preguntas de opcion multiple. Los tres botones son las
tres opciones: izquierdo = A, centro (OK) = B, derecho = C. No hay limite de tiempo ni penalizacion:
si se falla, se muestra la respuesta correcta; al final se ve el puntaje y el tiempo medio de
respuesta. Cada ronda empieza en una pregunta distinta del banco. Una caida o un aviso de
medicamento interrumpen el juego.

Para cambiar las preguntas se edita solo `lib/cogni_game/game_content.c` (sin tildes ni enie,
lineas de hasta 21 caracteres, opciones de hasta 17; `pio test -e native` lo verifica). Las 3
primeras preguntas son personales y de ejemplo: hay que reemplazarlas por datos reales del
paciente. `desk_app_game_result()` entrega el ultimo resultado (`revision` cambia al terminar cada
ronda) para publicarlo a la nube.

## Configuracion Wi-Fi

`WifiManager` intenta las dos credenciales EEPROM guardadas y, si ninguna conecta, inicia el
punto de acceso `CogniDesk-Setup`. La clave temporal del AP se imprime por Serial; desde el
celular se abre `http://192.168.4.1`. El formulario mantiene la interfaz del Desk activa y solo
reemplaza el espacio de credenciales anterior despues de confirmar una conexion exitosa, por lo
que un intento fallido no borra la red guardada. El almacenamiento conserva el formato de dos
espacios de la libreria compartida; por ese formato la clave ingresada admite hasta 49 caracteres.

El router debe estar fijado en canal 1, igual que ESP-NOW en el Desk y la pulsera. El gestor usa
`WiFi.begin(..., DESK_WIFI_CHANNEL)`. `ClockManager` sincroniza la hora local por NTP en UTC-5;
sin conexion y sin una sincronizacion previa, la pantalla muestra `--:--` y las alarmas no se
disparan. `include/apwifieeprommode.h` se conserva como referencia de la libreria recibida, pero
no se incluye: su flujo es bloqueante, fija canal 6 y escribe la contrasena de Wi-Fi por Serial.

En la opcion Reloj, OK abre la edicion de la alarma seleccionada, derecha cambia entre las tres
alarmas y izquierda vuelve al menu. En la edicion, izquierda/derecha cambian hora, minuto o
estado; OK avanza entre esos campos y guarda al confirmar el estado. Las alarmas se repiten a
diario y se almacenan en NVS. OK cierra el aviso de medicamento, pero no confirma que se haya
tomado.

## Limite de integracion con la nube

La implementacion de MQTT/ThingsBoard corresponde al companero responsable de nube. Para mantener
responsabilidades separadas, `CommManager` conserva el transporte ESP-NOW, la validacion y la
deduplicacion; el futuro adaptador de nube debe recibir solo mensajes nuevos ya decodificados:
telemetria (`alba_telemetry_t`) y eventos (`alba_event_t`). El formato y topico existentes estan
definidos por `../common/alba_thingsboard.h` (`ALBA_TB_TOPIC`). La conversion y publicacion no deben
bloquear el callback ESP-NOW ni impedir que el flujo local de caidas atienda el boton.

Wi-Fi y ESP-NOW comparten la radio: la conexion al router debe permanecer en canal 1 o se tendra
que acordar y configurar otro canal en los dos dispositivos. El token de ThingsBoard debe
inyectarse fuera del repositorio; las credenciales Wi-Fi se guardan en EEPROM y el AP de
configuracion usa una clave aleatoria temporal. La pantalla solo debera afirmar que el cuidador
recibio el aviso despues de confirmar el resultado de publicacion.

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
 robot_fall:  SHOW_PROMPT -> pantalla "¿Estas bien?"       | boton -> robot_fall_on_button_ok
              SEND_CANCEL -> alba_pack_cancel(seq++, ref_seq) -> esp_now_send   (se repite solo, ver abajo)
               SHOW_HELP_CALLED -> publicar alerta confirmada | HIDE_PROMPT -> quitar la pregunta
```

 En `cogni-desk`, `SHOW_HELP_CALLED` representa que la pulsera confirmo la caida; la publicacion
 MQTT debe completarse antes de indicar en pantalla que el cuidador fue notificado.

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
    case ROBOT_ACT_SHOW_HELP_CALLED: ui_show_fall_confirmed_pending(); break; // confirmar aviso tras publicar en MQTT
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
4. **Canal y MAC STA.** La pulsera usa unicast al peer `44:1B:F6:8C:AC:04`
   en el canal 1 (`firmware/cogni-band/include/band_config.h`). Esa MAC debe ser
   la interfaz STA del ESP32 del robot; el robot debe transmitir desde STA y estar
   fijado al canal 1. Si el robot se conecta a un router en otro canal, ESP-NOW
   dejara de comunicar hasta que ambos vuelvan al mismo canal. La pulsera imprime
   su MAC STA al arrancar; el robot debe agregar esa MAC como peer para enviar HORA
   y CANCELAR.
5. **No usar `robot_link` / `robot_fall` desde dos tareas a la vez**: no son thread-safe.
6. **Credenciales** (token de ThingsBoard, clave del WiFi): nunca en el repositorio (es publico).

## Pruebas

En el PC, sin placa:
- `cd firmware/cogni-desk && pio test -e native` (32 pruebas del enlace y del asistente de caidas).
- `cd firmware/cogni-band && pio test -e native -f test_robot_roundtrip` (pulsera y robot juntos: caida, cancelar,
  cancelar perdido en el aire, duplicados, hora).

## Nube (ThingsBoard)

El robot publica por MQTT a ThingsBoard desde `CloudManager` (tarea FreeRTOS en el nucleo 0, no bloquea botones, pantallas ni buzzer).

| Origen | Claves en ThingsBoard |
|---|---|
| Telemetria de la pulsera | `bpm`, `spo2`, `bateria`, ... (ver `alba_thingsboard.h`) |
| Evento de la pulsera (caida) | alerta, valor pico, confirmacion |
| DHT11 del robot (cada 30 s) | `room_temp` (°C), `room_hum` (%) |
| CogniGame, al terminar una ronda | `game_score`, `game_total`, `game_avg_s` |

Configuracion (una sola vez por computador):

1. Copiar `firmware/cogni-desk/include/cloud_secrets.example.h` como `cloud_secrets.h` (esta en `.gitignore`, no se sube).
2. Completar host, puerto, TLS y el **token** del dispositivo (ThingsBoard -> Devices -> Pulsera ALBA -> Copy access token).
3. Compilar y subir. Sin `cloud_secrets.h` la nube queda desactivada y lo demas funciona igual.

Detalles: cola de 16 mensajes en RAM (sin red descarta primero telemetria antigua y conserva las alertas); si el robot ya tiene hora NTP los mensajes llevan `ts`, si no, ThingsBoard usa la hora de llegada; TLS usa `setInsecure()` (cifra pero no verifica el servidor: solo pruebas).

## Canal automatico (hotspot del celular)

ESP-NOW y el Wi-Fi comparten la misma radio: cuando el robot se conecta a un router o al hotspot del celular, su radio pasa al canal de esa red, y ese canal puede ser cualquiera. Antes el robot forzaba el canal 1 y la conexion fallaba o la pulsera quedaba sorda.

Ahora:

- **Robot:** se conecta con `DESK_WIFI_STA_CHANNEL = 0` (el canal que use la red) y su peer ESP-NOW usa `channel = 0` (el canal actual). Si no hay Wi-Fi, queda en el canal 1.
- **Pulsera:** envia unicast y espera el ACK de la radio (`esp_now_send` + callback). Con 2 envios seguidos sin ACK busca el canal: prueba 6, 11, 1..13 con el mismo paquete hasta que el robot responde, y se queda en ese canal (`lib/band_channel`). La busqueda tarda como maximo ~1.5 s y ocurre solo dentro de la tarea de radio.
- **Si el robot cambia de canal** (se conecta al Wi-Fi, o el hotspot cambia), la pulsera lo vuelve a encontrar sola en el siguiente envio (la telemetria sale cada 30 s).

### Diagnostico

Cada 10 s el robot imprime por serial:

```
[diag] canal=6 wifi=si nube=si recibidos=42 perdidos=3 (7%) reinicios_pulsera=0
```

`perdidos` sale de los saltos del contador `seq` de la pulsera (`lib/link_stats`). Cada 60 s la pulsera imprime:

```
[radio] canal=6 ok=40 sin_ack=5 busquedas=2 cambios=1
```

Cuando cambia de canal ademas escribe `[radio] robot encontrado en canal N (antes M)`.

**Importante:** la pulsera debe compilarse con `BAND_COMM_MODE_ESPNOW` (en `platformio.ini` de la pulsera figura `BAND_COMM_MODE_WIFI_DEBUG`, que NO usa ESP-NOW).

## Hora para la pulsera

La pulsera no tiene reloj propio: usa la hora del robot para su horario de reposo (modo noche/dia). El robot obtiene la hora por NTP (`ClockManager`, necesita Wi-Fi al menos una vez desde el arranque) y se la manda con el mensaje `HORA` (UTC en segundos + zona `DESK_UTC_OFFSET_MIN = -300` minutos, Ecuador).

- `robot_link` decide **cuando**: al aparecer la pulsera (3 envios seguidos), tras un reinicio de la pulsera y despues cada 6 h.
- `CommManager::actualizarHora()` la envia por ESP-NOW y solo la da por entregada si la radio de la pulsera devuelve el **ACK**; si no, reintenta cada 5 s.
- Sin hora NTP todavia, el robot imprime una vez `[hora] la pulsera pide la hora, pero el robot aun no tiene hora NTP` y espera.

Salida esperada en el serial del robot:

```
[hora] HORA enviada: epoch=1790000000 zona=-300 min
[hora] HORA recibida por la pulsera (ACK)
```

y en el de la pulsera: `[comm] hora recibida del robot: epoch=... zona=-300 min -> hora local N (ok)`.

## Estado del aviso de caida

La pantalla "CAIDA CONFIRMADA" ya no dice siempre "pendiente de red": muestra lo que paso con el aviso al cuidador (el evento de caida CONFIRMADA que se publica en ThingsBoard) y se actualiza sola.

| Pantalla (OLED derecho) | Significado |
|---|---|
| `Enviando aviso` / `al cuidador...` | Esta en la cola y hay conexion con la nube; sale en un instante |
| `SIN RED: aviso` / `en cola, reintenta` | No hay Wi-Fi o conexion con ThingsBoard; el aviso sigue en cola y sale al volver la red |
| `Aviso ENVIADO` / `al cuidador` | El robot lo publico por MQTT en ThingsBoard |
| `Aviso NO enviado` / `Llama al cuidador` | No llego a la nube un aviso confirmado (por ejemplo se perdio el paquete CONFIRMADA de la pulsera) o se descarto de la cola llena |
| `Nube sin configurar` / `Llama al cuidador` | Falta `include/cloud_secrets.h` |

"Enviado" quiere decir que el robot lo publico; que ThingsBoard dispare la alarma o el correo se confirma en su dashboard. La logica esta en `lib/cloud_outbox` (`cloud_outbox_alert_status`, `cloud_alert_show`) y se prueba en el PC.

## Despertar y ejercicios de orientacion

La propuesta pide que, al detectar la transicion sueno-vigilia, el robot active la alarma de medicacion y la estimulacion cognitiva.

- La pulsera (`wake_detector`) manda el evento `VIGILIA_TRIGGER`. Antes el robot lo ignoraba (solo publicaba a la nube).
- Ahora `desk_app_on_wake()` muestra la pantalla **BUENOS DIAS** (hora, "Hora de su medicina"), suena el buzzer con el patron de medicamento (maximo 60 s) y ofrece el juego: **OK** empieza, **IZQ** lo pospone.
- No interrumpe una caida, una alarma de medicamento, la edicion de alarmas ni una partida en curso, y ignora despertares repetidos durante 30 min (`DESK_WAKE_COOLDOWN_MS`).
- Prueba sin esperar a la manana: escribir `w` en el monitor serial del robot.

### Preguntas de orientacion (CogniGame)

Si el robot tiene hora NTP, cada ronda empieza con 3 preguntas de orientacion armadas con la fecha real (`lib/cogni_game/orientation.c`) y sigue con 2 del banco personal (`game_content.c`):

1. Que dia de la semana es hoy (Lunes ... Domingo)
2. Que fecha es hoy (numero del dia del mes)
3. Que momento del dia es ahora (Manana 5-11 h, Tarde 12-18 h, Noche 19-4 h)

Siempre 3 opciones cerradas (botones IZQ / OK / DER). Las opciones falsas son dias, fechas y momentos validos y distintos entre si, y la posicion de la correcta cambia entre rondas. Sin hora NTP la ronda usa solo el banco de preguntas.
