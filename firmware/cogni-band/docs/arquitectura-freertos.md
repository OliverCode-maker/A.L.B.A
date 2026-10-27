# Arquitectura FreeRTOS de la pulsera (cogni-band)

Placa: **ESP32-S3 Super Mini** (PlatformIO con ESP-IDF: `lolin_s3_mini`).
I2C en SDA = GPIO6, SCL = GPIO7;
MPU6050 y MAX30102 comparten el bus y no tienen el pin INT conectado (se leen por sondeo).
Pines en `include/band_pins.h`, parametros en `include/band_config.h`.

## Idea en una frase

Cuatro tareas, cada una con su ritmo y sus propios datos; se pasan informacion solo por
colas o por funciones con mutex. Los detectores (caidas, despertar) son C puro, sin
FreeRTOS, y ya estan probados en el PC.

## Diagrama

```
 IMU --100 Hz--> tFall ----------------+
                  |  ^                  |  qTx (alertas, telemetria)
   actividad      |  | qCmd             v
   (cada 1 s)     |  | (CANCELAR)    +-------+  ESP-NOW   +-------+
                  v  |               | tComm | ---------> | robot |
 PPG --15..300 s-> tVitals ---------> |       | <--------- |       |
                  ^    |   qTx        +-------+ HORA,      +-------+
   hora local     |    v latest          ^     CANCELAR
   (band_clock)   |   tSystem            | qRx (callback de la radio)
                       telemetria + bateria + estadisticas
```

## Tareas

| Tarea     | Prioridad | Periodo                              | Pila  | Que hace |
|-----------|-----------|--------------------------------------|-------|----------|
| `tFall`   | 5 (alta)  | 10 ms exactos (`vTaskDelayUntil`)    | 4 kB  | Lee el IMU, corre el detector de caidas, pasa la actigrafia cada 1 s |
| `tComm`   | 4         | espera `qTx` (20 ms max)             | 4 kB  | Unica duena de la radio: arma paquetes, envia, reintenta alertas, procesa HORA y CANCELAR |
| `tVitals` | 3         | 60 s dormido / 15 s sospecha / 300 s despierto (lo dicta el detector) | 4 kB | Mide pulso, corre el detector de despertar |
| `tSystem` | 2 (baja)  | 1 s                                  | 4 kB  | Telemetria cada 30 s, nivel de bateria y aviso de bateria baja, estadisticas |

`tFall` es la mas urgente porque la caida se detecta con ventanas de decenas de milisegundos
(caida libre >= 60 ms): si se atrasa, se pierde el evento. Una alerta no debe esperar detras
de la telemetria, por eso `tComm` va por encima de `tVitals`.

## Quien es dueno de que (regla de oro)

| Dato | Dueno | Otro que lo usa | Como |
|------|-------|-----------------|------|
| detector de caidas | tFall | - | nadie mas lo toca |
| detector de despertar | tVitals | - | nadie mas lo toca |
| reloj (`band_clock`) | tComm (escribe al recibir HORA) | tVitals (lee la hora) | mutex |
| actigrafia (`band_activity`) | tFall (suma) | tVitals (consume) | mutex |
| ultimo pulso/SpO2 | tVitals | tSystem | mutex |
| compuerta de cancelacion | tComm | - | nadie mas |
| cancelar una caida | tComm -> tFall | | cola `qCmd` |
| alertas y telemetria a enviar | cualquiera -> tComm | | cola `qTx` |
| mensajes del robot | radio -> tComm | | cola `qRx` (el callback solo copia y vuelve) |

Los detectores **no son thread-safe**: por eso cada uno vive en una sola tarea y las demas le
piden cosas por cola (p. ej. cancelar una caida) en vez de llamar a su funcion.

## Flujos importantes

**Caida.** `tFall` detecta -> `PENDIENTE` por `qTx` -> `tComm` lo envia y arma la compuerta con
su `seq`. El robot muestra "¿Estas bien?". Si responde, manda `CANCELAR(ref_seq)`; `tComm` solo lo
acepta si coincide con la caida pendiente y avisa a `tFall` por `qCmd` -> `CANCELADA`. Si pasan 20 s
sin respuesta -> `CONFIRMADA`. Un CANCELAR repetido, tardio o de otra caida se ignora.

**Despertar.** `tVitals` toma pulso al ritmo que pide el detector, con la actigrafia de `tFall` y la hora
local de `band_clock` (si aun no hay hora, se omite solo la ventana 4-11 h). Un disparo -> `VIGILIA_TRIGGER`.

**Hora.** El robot envia `HORA` al emparejar y cada pocas horas; `band_clock` la sigue con `millis()` y
la da por desconocida si pasan 48 h.

## Que esta simulado y que es real

| Parte | Estado |
|-------|--------|
| Detectores, protocolo, codec, reloj, bateria, actigrafia | Real, con pruebas en el PC (`pio test -e native`) |
| Tareas y colas | Real; probadas con FreeRTOS en el PC (`tools/host_sim/check.sh`) |
| IMU y PPG | Reales por I2C con `BAND_SIM=0`; simulados con `BAND_SIM=1` |
| Drivers MPU6050 / MAX30102 | Integrados; validar lecturas y calibracion en la placa |
| ESP-NOW | Pendiente; en modo real `port_send()` devuelve `false` |
| Medicion de bateria | Pendiente de divisor y ADC; en modo real `port_battery_mv()` devuelve `0` |

La frontera esta en `src/band_port.h`: las tareas solo llaman a `port_*`.

## Como verlo

**Prueba de sensores en la placa:** el entorno `lolin-s3-mini` compila con
`BAND_SIM=0`; ejecuta `pio run -e lolin-s3-mini -t upload` y
`pio device monitor -e lolin-s3-mini`. El MPU6050 imprime aceleracion y giroscopio
cada segundo; el MAX30102 imprime BPM, SpO2 estimada y calidad al completar cada lectura.
ESP-NOW y la medicion de bateria aun no estan integrados en este modo.

**En el PC (sin placa):** `tools/host_sim/run.sh` ejecuta las mismas tareas sobre FreeRTOS real (puerto
POSIX; la primera vez descarga el kernel de GitHub). `tools/host_sim/check.sh` lo corre y comprueba que
paso todo lo esperado. `FAST=0 tools/host_sim/run.sh 1500` usa tiempos reales.

## Por decidir / pendiente

1. **Nucleos del S3.** Hoy las tareas no estan ancladas (`xTaskCreate`). Con ESP-NOW activo, probar
   `tFall` en el nucleo 1 y la radio en el 0 (`xTaskCreatePinnedToCore`) y medir la fluidez a 100 Hz.
2. **Bus I2C compartido.** IMU (100 Hz) y PPG (rafagas largas) usan el mismo bus con mutex.
   `port_ppg_read` cede CPU entre bloques para que `tFall` mantenga su periodo.
3. **ACK de eventos criticos.** Hoy se reintenta si falla el envio local; falta que el robot confirme la recepcion.
4. **Modos de energia** (ACTIVO / REPOSO / NO PUESTA) y la tabla de consumo corregida para el informe.
5. **Tamano de pilas.** Medir en la placa (`uxTaskGetStackHighWaterMark`, se imprime cada 30 s) y ajustar.
6. **Boton / vibrador / LED.** Sin definir; mientras tanto la cancelacion es desde el robot.
