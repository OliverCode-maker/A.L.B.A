# Arquitectura FreeRTOS de la pulsera (cogni-band)

Placa: **ESP32-S3 Super Mini** (PlatformIO con ESP-IDF: `lolin_s3_mini`).
I2C en SDA = GPIO6, SCL = GPIO7;
MPU6050 y MAX30102 comparten el bus y no tienen el pin INT conectado (se leen por sondeo).
Pines en `include/band_pins.h`, parametros en `include/band_config.h`.

## Idea en una frase

Cinco tareas, cada una con su ritmo y sus propios datos; se pasan informacion solo por
colas o por funciones con mutex. La tarea del IMU no ejecuta la inferencia: entrega
ventanas a `tFallModel`, que corre TensorFlow Lite Micro fuera del ciclo de 100 Hz.

## Diagrama

```
 IMU --100 Hz--> tFall --qModelWindow--> tFallModel
                  ^                       |
                  +------qModelResult-----+
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

| Tarea        | Prioridad | Periodo                              | Pila  | Que hace |
|--------------|-----------|--------------------------------------|-------|----------|
| `tFall`      | 5 (alta)  | 10 ms exactos (`vTaskDelayUntil`)    | 4 kB  | Lee el IMU, mantiene la ventana 200x6 y pasa la actigrafia cada 1 s |
| `tComm`      | 4         | espera `qTx` (20 ms max)             | 4 kB  | Unica duena de la radio: arma paquetes, envia, reintenta alertas, procesa HORA y CANCELAR |
| `tVitals`    | 3         | 60 s dormido / 15 s sospecha / 300 s despierto (lo dicta el detector) | 4 kB | Mide pulso, corre el detector de despertar |
| `tSystem`    | 2 (baja)  | 1 s                                  | 4 kB  | Telemetria cada 30 s, nivel de bateria y aviso de bateria baja, estadisticas |
| `tFallModel` | 2 (baja; 6 solo al iniciar) | cada 25 muestras (250 ms) | 16 kB | Normaliza y ejecuta el modelo int8; devuelve su puntuacion a `tFall` |

`tFall` es la mas urgente para muestrear el IMU cada 10 ms. La ventana se entrega con una
cola de longitud uno (si el modelo se atrasa, conserva la mas reciente); la inferencia corre
a prioridad baja para no bloquear el muestreo. Una alerta no debe esperar detras
de la telemetria, por eso `tComm` va por encima de `tVitals`.

## Quien es dueno de que (regla de oro)

| Dato | Dueno | Otro que lo usa | Como |
|------|-------|-----------------|------|
| detector de caidas | tFall | - | nadie mas lo toca |
| interprete y tensor arena TFLite | tFallModel | - | nadie mas |
| ventanas IMU para inferencia | tFall -> tFallModel | - | cola `qModelWindow` |
| puntuaciones del modelo | tFallModel -> tFall | - | cola `qModelResult` |
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

**Caida.** `tFall` agrega aceleracion y giroscopio a una ventana de 2 s (200x6).
Cada 25 muestras envia una ventana a `tFallModel`; una puntuacion >= 0.6 genera
`PENDIENTE` por `qTx` -> `tComm` lo envia y arma la compuerta con
su `seq`. El robot muestra "¿Estas bien?". Si responde, manda `CANCELAR(ref_seq)`; `tComm` solo lo
acepta si coincide con la caida pendiente y avisa a `tFall` por `qCmd` -> `CANCELADA`. Si pasan 20 s
sin respuesta -> `CONFIRMADA`. Un CANCELAR repetido, tardio o de otra caida se ignora.

El modelo usa solo `AccX/Y/Z` en g y `GyrX/Y/Z` en grados/s, en ese orden y a 100 Hz.
Antes de guardar cada muestra, tFallModel rota los ejes del MPU6050 al marco del
dataset: `(X, Y, Z) = (MPU.X, -MPU.Z, MPU.Y)` tanto para aceleracion como para giroscopio.
Esta rotacion se eligio porque, en las lecturas en reposo, la gravedad aparece en `+Z`
de la pulsera y en `-Y` en el entrenamiento; requiere mantener la misma orientacion fisica.
Antes de cuantizar, entrenamiento y firmware normalizan cada canal con media/desviacion
calculadas solo sobre sujetos de entrenamiento; estos valores se generan en
`models/fall_detector/fall_model_preprocess.h`.
El umbral int8 por defecto es 0.6 para reducir falsos positivos; subirlo reduce tambien
el recall y debe calibrarse con datos de movimientos reales de la pulsera.
Para depurar en la placa, tFall imprime una vez por segundo la probabilidad, el pico de
aceleracion de la ventana y el umbral, incluso si no se genera una alerta. Estas puntuaciones
sirven para diagnostico; para reentrenar se necesitan tambien muestras IMU crudas etiquetadas.

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
| Modelo de caidas | TFLite Micro int8; validar inferencia, latencia y falsas alarmas en la placa |
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

**Resultados por Wi-Fi, sin cable serial:** al arrancar, la pulsera crea el punto de acceso
abierto `ALBA-Fall-Debug` (IP `192.168.4.1`). Conecta la computadora a esa red y ejecuta
`python tools/receive_fall_results.py`; el programa escucha UDP en el puerto `3333` y muestra
una linea JSON por ventana con la clase algorítmica, predicción/probabilidad del modelo y pico
de aceleración. Cuando el detector confirma dos ventanas consecutivas, envía además un evento
`fall_candidate`; el receptor muestra **CAIDA PROBABLE DETECTADA** y pausa la salida normal
durante tres segundos mientras sigue recibiendo datos. El firmware transmite por broadcast a
`192.168.4.255:3333`; permite UDP entrante en el firewall de la computadora si el sistema lo
solicita. La consola serial se
mantiene disponible y el resto de mensajes no se envía por Wi-Fi. Para que quepan el modelo
y Wi-Fi en la flash de 2 MB, se usa una partición de aplicación única de 1500 KB; no hay
actualización OTA en esta configuración temporal.

**En el PC (sin placa):** `tools/host_sim/run.sh` ejecuta las mismas tareas sobre FreeRTOS real (puerto
POSIX; la primera vez descarga el kernel de GitHub). `tools/host_sim/check.sh` lo corre y comprueba que
paso todo lo esperado. `FAST=0 tools/host_sim/run.sh 1500` usa tiempos reales.

**Entrenar y exportar el modelo del firmware:** instala `tools/requirements-fall-model.txt`
y ejecuta `python tools/train_firmware_fall_model.py --data-root "C:\ruta\fallarchive"`.
El conjunto debe contener `Sample_Training/<sujeto>/*.csv` y
`Sample_Test/<sujeto>/*.csv`. Se reservan sujetos completos de entrenamiento para
validacion y se mantiene `Sample_Test` como prueba independiente. El script genera ventanas
de 200x6, entrena la CNN Conv1D y exporta TFLite int8, metricas, metadatos y el arreglo C
enlazado al firmware. Los CSV deben tener muestreo de 100 Hz y la columna `FallCheck`;
el firmware aplica la normalizacion por canal generada por el script. De forma predeterminada,
se agrega una rotacion 3D aleatoria por ventana de entrenamiento, aplicando la misma rotacion a
acelerometro y giroscopio, y una ganancia de aceleracion entre 0.85 y 1.15; no se modifican
validacion ni prueba. Se pueden ajustar con `--orientation-augmentation N` (N=0 desactiva la
aumentacion) y `--acceleration-gain-min` / `--acceleration-gain-max`. El script tambien evalua
el modelo TFLite int8 en 26 orientaciones, siete magnitudes de gravedad entre 0.85 y 1.1 g y
dos perfiles de giroscopio; registra las probabilidades maximas en `metadata.json`. Esta prueba
sintetica es una comprobacion de regresion, no sustituye pruebas con la pulsera.

Este entrenamiento usa etiquetas binarias `0=sin FallCheck` y `1=al menos un FallCheck`
en la ventana. No se ha confirmado que `FallCheck` represente tambien pre-impacto; el modelo
no debe interpretarse como detector validado de pre-impacto. La precision, latencia y tasa de
falsas alarmas aun requieren validacion con datos y pruebas en la pulsera.

En tiempo de ejecucion, una clasificacion algoritmica de tres clases y el modelo
TensorFlow binario procesan cada ventana IMU de 200x6 en paralelo. Las reglas usan
`A_mag` y `G_mag`, los umbrales configurables de `include/band_config.h`, la secuencia
caida libre-impacto y una confirmacion posterior por giro, cambio de orientacion o
reposo. Devuelven `no-caida`, `pre-impacto` o `caida`; el modelo FallArchive devuelve
la probabilidad binaria `FallCheck`. El log `[comparacion]` alinea las predicciones
por la misma ventana e informa la distribucion de clases de reglas y TP/FN/FP/TN entre
las ventanas clasificadas `no-caida` o `caida`. Las ventanas `pre-impacto` se excluyen
de esa matriz porque el modelo binario no tiene esa clase. Es concordancia entre
predictores, no exactitud contra verdad-terreno. Solo el modelo TensorFlow genera
la puntuacion de la alerta, pero el firmware ahora solo abre una ventana de cancelacion
cuando el modelo y las reglas clasifican `caida` en dos ventanas consecutivas (maximo
500 ms entre ellas). Esta concordancia puede reducir falsos positivos al levantar el brazo,
pero tambien puede omitir caidas que las reglas no reconozcan; requiere validacion en la muneca.

`tools/train_fall_model.py` ya no entrena TensorFlow para Three Classes: evalua un
clasificador algorítmico por magnitud sobre su split de prueba. Ejemplo:
`python tools/train_fall_model.py --data-root "C:\ruta\Three Classes"`.
Calcula la norma de sus tres canales. Además de reportar las reglas iniciales (0.5 y
2.5 en unidades crudas, no verificadas como g), calibra umbrales candidatos con
cuantiles de entrenamiento y selecciona por macro-F1 en validación. Como no se ha
confirmado el significado de los canales ni el orden de las etiquetas one-hot, calibra
y reporta las seis permutaciones posibles, sin escoger una como verdadera; las métricas
finales se calculan en test.

El mismo reporte recopila las métricas de prueba del modelo TensorFlow binario
entrenado con FallArchive desde `models/fall_detector/test_metrics.json`. Los resultados
se mantienen separados en `models/three_classes_rules/comparison.json`: sus datasets,
entradas y etiquetas son distintos, por lo que esas métricas no permiten afirmar cuál
clasificador es más preciso sobre las mismas muestras. Para una comparación directa
hace falta un conjunto común con canales, unidades y etiquetas verificados, idealmente
capturado con la pulsera en la muñeca.

## Por decidir / pendiente

1. **Nucleos del S3.** Hoy las tareas no estan ancladas (`xTaskCreate`). Con ESP-NOW activo, probar
   `tFall` en el nucleo 1 y la radio en el 0 (`xTaskCreatePinnedToCore`) y medir la fluidez a 100 Hz.
2. **Bus I2C compartido.** IMU (100 Hz) y PPG (rafagas largas) usan el mismo bus con mutex.
   `port_ppg_read` cede CPU entre bloques para que `tFall` mantenga su periodo.
3. **ACK de eventos criticos.** Hoy se reintenta si falla el envio local; falta que el robot confirme la recepcion.
4. **Modos de energia** (ACTIVO / REPOSO / NO PUESTA) y la tabla de consumo corregida para el informe.
5. **Tamano de pilas.** Medir en la placa (`uxTaskGetStackHighWaterMark`, se imprime cada 30 s) y ajustar.
6. **Boton / vibrador / LED.** Sin definir; mientras tanto la cancelacion es desde el robot.
