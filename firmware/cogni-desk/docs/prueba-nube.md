# Prueba pulsera -> robot -> nube (ThingsBoard)

Continua la prueba de enlace (`prueba-enlace.md`): ahora el robot se conecta a WiFi y publica lo que recibe de la
pulsera al panel del cuidador. Hace falta que la prueba de enlace ya funcione.

## Que se necesita
- Pulsera con el firmware `link-test` (sensores simulados, radio real) y robot con `linktest-nube`.
- Una red WiFi de **2.4 GHz** con internet y clave sencilla. **Recomendado: el hotspot de un celular.**
  Las redes institucionales (con usuario/portal, p. ej. WPA2-Enterprise) no sirven con `WiFi.begin(ssid, clave)`.
- El **token del dispositivo** de ThingsBoard (dispositivo "Pulsera ALBA": Devices -> Copy access token).
- El panel y las alarmas ya creados (cloud/README.md).

## Pasos
1. **Credenciales del robot** (no se suben al repo):
   ```bash
   cd firmware/cogni-desk
   cp include/comm_secrets.example.h include/comm_secrets.h
   ```
   Editar `comm_secrets.h`: `COMM_WIFI_SSID`, `COMM_WIFI_PASS`, `COMM_TB_TOKEN` (y dejar `COMM_TB_HOST "mqtt.thingsboard.cloud"`, puerto 8883).
2. **Cargar el robot:** `pio run -e linktest-nube -t upload` y `pio device monitor`.
   Debe imprimir: `WiFi conectado | canal N | IP ...` y despues `MQTT conectado a ThingsBoard`.
3. **El canal:** anotar el `N` que imprime el robot. La pulsera debe usar **ese mismo canal**: en
   `firmware/cogni-band/platformio.ini`, entorno `link-test`, cambiar `-DBAND_ESPNOW_CHANNEL=1` por `-DBAND_ESPNOW_CHANNEL=N`,
   y recargar la pulsera: `pio run -e link-test -t upload`. (Si el canal del router cambia, hay que repetirlo.)
4. **Reiniciar la pulsera** (RESET) y mirar el monitor del robot: cada paquete aparece como `RX de la pulsera` y despues
   como `-> nube: {...}`. La linea `[test]` cada 5 s muestra `WiFi si | nube si | publicados=N`.
5. **En ThingsBoard:** el panel debe mostrar pulso, SpO2 y bateria; el registro de eventos, las caidas; y en Alarms
   la "Caida confirmada" (si en la caida 2 no se pulsa BOOT) o nada si se cancela. Cerca del final del guion
   la bateria simulada baja y deberia aparecer "Bateria baja".

## Si algo falla
| Sintoma | Causa probable |
|---|---|
| `WiFi` nunca conecta | red 5 GHz o de institucion; SSID/clave mal; usar hotspot 2.4 GHz |
| `MQTT no conecto (estado -2/-4)` | sin internet, hora/TLS, host o puerto mal; probar `ping` desde otra PC al mismo WiFi |
| `MQTT no conecto (estado 5)` | token incorrecto (no autorizado) |
| El robot se conecta pero no recibe nada de la pulsera | **canal distinto** (paso 3) |
| Se recibe pero no aparece en el panel | `publicados=0`: ver el estado MQTT; si `publicados>0`, revisar el dispositivo/token en ThingsBoard |
| Los datos llegan con la hora vieja o sin hora | el robot no obtuvo hora por internet (NTP); los mensajes salen sin `ts` y ThingsBoard usa su hora |

## Cosas a saber
- Cada intento de reconectar MQTT puede bloquear el robot ~5 s (como maximo cada 30 s). En la prueba no importa; en el
  robot final hay que moverlo a una tarea aparte.
- Con la guia rapida la pulsera manda telemetria cada 5 s; el plan gratuito de ThingsBoard tiene limite de datos: no
  dejarlo corriendo horas.
- Sin certificado de la CA, el TLS cifra pero no verifica el servidor (solo pruebas).
- **Esto solo se reviso de sintaxis**: no se ha compilado con el SDK real ni probado en placas.
