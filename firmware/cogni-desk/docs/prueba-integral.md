# Prueba integral pulsera + robot + nube

Guia para probar en hardware todo lo agregado: Salud, buzzer, CogniGame, nube, canal automatico, hora y estado del aviso de caida. Hacer los pasos **en orden**; si uno falla, copiar las lineas del monitor serial indicadas y no seguir al siguiente.

## 0. Preparacion

| Quien | Tarea |
|---|---|
| Ambos | `git pull` de la rama `feat/robot-salud-juego` (con los parches de nube, canal, hora y estado del aviso). |
| Gaby | En `firmware/cogni-band/platformio.ini` cambiar `-DBAND_COMM_MODE=BAND_COMM_MODE_WIFI_DEBUG` por `-DBAND_COMM_MODE=BAND_COMM_MODE_ESPNOW`. Compilar y subir. Monitor a 115200. |
| Oliver | Copiar `firmware/cogni-desk/include/cloud_secrets.example.h` como `cloud_secrets.h` y poner el token del dispositivo "Pulsera ALBA" (ThingsBoard -> Devices -> Copy access token). Compilar y subir. |
| Ambos | Hotspot del celular en 2.4 GHz, con datos moviles. El robot se configura con ese Wi-Fi (AP `CogniDesk-Setup`). |

Si no compila, pegar el error completo (primeras 30 lineas) al chat.

## 1. Enlace pulsera -> robot (sin nube)

1. Encender el robot y despues la pulsera.
2. Robot debe imprimir `[pulsera] BPM=... SpO2=... bateria=...%` cada ~30 s y, cada 10 s, `[diag] canal=N wifi=si ... recibidos=X perdidos=Y (Z%)`.
3. **Resultado a anotar:** el porcentaje de perdidos tras 5 minutos. Meta: menos de 10 %.

Falla comun: la pulsera sigue en `WIFI_DEBUG` (no aparece `[esp-now] listo` en su serial).

## 2. Canal automatico

1. Con el robot conectado al hotspot, mirar `canal=N` en `[diag]` (puede ser distinto de 1).
2. Pulsera debe imprimir `[radio] robot encontrado en canal N (antes 1)` y luego seguir con `[radio] canal=N ok=... sin_ack=...`.
3. Apagar y encender el hotspot: la pulsera debe volver a encontrar el robot sola (puede cambiar de canal).

Copiar si falla: lineas `[radio]` de la pulsera y `[diag]` + `[wifi]` del robot de los mismos 5 minutos.

## 3. Hora

1. Con el robot ya conectado al Wi-Fi: robot `[hora] HORA enviada: epoch=... zona=-300 min` y `[hora] HORA recibida por la pulsera (ACK)`.
2. Pulsera: `[comm] hora recibida del robot: epoch=... zona=-300 min -> hora local N (ok)`. Comprobar que la hora local es la real de Ecuador.

## 4. Pantallas y buzzer del robot

1. Menu -> **Salud**: muestra BPM, SpO2 y bateria; con la pulsera apagada (90 s) muestra "Sin senal".
2. Menu -> **CogniGame**: jugar una ronda de 5 preguntas con los 3 botones. Ver que el texto cabe en las dos pantallas.
3. Buzzer: suena en alarma de medicamento y en caida (patrones distintos).

## 4b. Despertar y ejercicios de orientacion

1. Con el robot en reposo, escribir `w` en el monitor serial: simula el despertar. Debe verse "BUENOS DIAS / Son las HH:MM / Hora de su medicina" y sonar el buzzer.
2. OK -> empieza el juego: las 3 primeras preguntas son de **orientacion** (dia de la semana, fecha, momento del dia) y deben coincidir con la fecha y hora reales; las 2 ultimas son del banco de preguntas. IZQ en el saludo lo pospone.
3. Sin hora NTP (sin Wi-Fi) el juego usa solo el banco de preguntas.
4. El despertar real: la pulsera manda `VIGILIA_TRIGGER`; el robot imprime `[despertar] la pulsera detecto el despertar (bpm=N)` y muestra la misma pantalla. Se ignoran avisos repetidos por 30 min, y nunca interrumpe una caida, el medicamento ni una partida en curso.

## 5. Nube (ThingsBoard)

1. Robot: `[nube] conectado a ThingsBoard` y lineas `[nube] -> {...}`.
2. ThingsBoard -> Devices -> Pulsera ALBA -> Latest telemetry: `bpm`, `spo2`, `bat`, `room_temp`, `room_hum`.
3. Terminar una ronda del juego: aparecen `game_score`, `game_total`, `game_avg_s`.

## 6. Caida y estado del aviso

1. Simular una caida con la pulsera (o usar el modo de prueba de Gaby). El robot muestra "POSIBLE CAIDA / Estas bien?".
2. **Prueba A (cancelar):** pulsar OK antes de 20 s. El robot vuelve al reposo; en la pulsera `[comm] CANCELAR aceptado`.
3. **Prueba B (confirmar):** no pulsar nada. Pantalla "CAIDA CONFIRMADA" con "Enviando aviso" y luego **"Aviso ENVIADO al cuidador"**. En ThingsBoard aparece el evento.
4. **Prueba C (sin red):** repetir B con el hotspot apagado. Debe decir "SIN RED: aviso en cola"; al encender el hotspot cambia solo a "ENVIADO".

## 7. Que enviar al terminar

- Porcentaje de perdidos del paso 1 y lineas `[diag]` / `[radio]` del paso 2.
- Que pasos funcionaron y cuales no, con las lineas del serial del que fallo.
