# CommManager (comunicaciones del robot)

Conecta el robot con la pulsera (ESP-NOW) y con el panel del cuidador (ThingsBoard por MQTT/TLS).

```
 pulsera --ESP-NOW--> onEspNowRecv (solo copia a una cola)
                          |
 loop():  procesarComunicacionesBackground()  -> comm_core (valida, descarta duplicados, asistente de caida)
          sincronizarDatosDashboardBackground() -> WiFi + MQTT, publica la cola hacia ThingsBoard
 pantallas/botones <- commPromptCaidaActivo(), commTomarAyudaAvisada(), commBpm() ...
```

* `lib/comm_core/` es C puro y esta probado en el PC: `pio test -e native -f test_comm_core` (19 pruebas).
* `src/CommManager.cpp` es la capa fina con WiFi, NTP, ESP-NOW y MQTT. **Solo se comprobo la sintaxis** con
  cabeceras de prueba; falta compilarla con PlatformIO y probarla en la placa.

## Poner en marcha
1. Copiar `include/comm_secrets.example.h` a `include/comm_secrets.h` y rellenar WiFi y token (no se sube al repo).
2. `pio run -e esp32-s3-devkitc-1` (baja la libreria PubSubClient).
3. Sin `comm_secrets.h` el robot compila igual pero no se conecta a nada.

## Lo que main.cpp ya llama y ahora existe
`initCommunication()`, `procesarComunicacionesBackground()`, `sincronizarDatosDashboardBackground()`,
`registrarTomaEnDashboard()`.

## Lo que la interfaz debe usar
* Mientras `commPromptCaidaActivo()` sea true: mostrar "¿Estas bien?" y, si se pulsa OK, llamar `commBotonEstoyBien()`.
  (Es el mismo boton OK del menu: durante la pregunta tiene prioridad.)
* `commTomarAyudaAvisada()` devuelve true una sola vez cuando la caida se confirmo: mostrar "ya avise a tu cuidador".
* Menu "Datos Cogni-Band": `commHayDatosPulsera()`, `commBpm()`, `commSpo2()`, `commBateria()`, `commPulseraOnline()`.
* Temperatura y humedad: llamar `commPublicarAmbiente(temp_x10, hum)` con la lectura del sensor (se envia como mucho 1 vez/min).
  Van como `room_temp` y `room_hum` al mismo dispositivo de ThingsBoard.

## Limitaciones conocidas
* **Canal WiFi:** con el robot conectado al router, ESP-NOW usa el canal del router. La pulsera debe usar el mismo
  (pendiente de acordar). El envio a la pulsera es por *broadcast* (sin emparejar MAC): cualquier pulsera cercana lo oye.
* `ensureMqtt` puede bloquear hasta ~5 s cuando intenta reconectar (cada 30 s como maximo). Si se nota en las pantallas,
  moverlo a una tarea aparte.
* Sin `COMM_TB_CA_CERT` el TLS cifra pero no verifica el servidor (solo pruebas).
* Los mensajes que esperan en la cola (16) se pierden si se reinicia el robot; la telemetria se descarta antes que las alertas.
* Si no hay hora real todavia, los mensajes salen sin `ts` y ThingsBoard usa la hora del servidor.
* Un solo dispositivo de ThingsBoard (el de la pulsera) recibe tambien los datos del cuarto y de medicacion.
