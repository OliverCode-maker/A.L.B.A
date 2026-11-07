# Prueba de enlace pulsera <-> robot (ESP-NOW)

Objetivo: comprobar que la pulsera y el robot **se hablan de verdad por radio**, sin sensores, sin pantallas,
sin WiFi del router y sin nube. Es la primera prueba de que el protocolo funciona fuera del PC.

## Que se necesita
- **2 placas ESP32-S3 con USB**: la de la pulsera (Super Mini de Gaby) y una del robot (la del devkit de Oliver, o cualquier S3).
- **Nada mas**: ni MPU6050, ni MAX30102, ni pantallas, ni router, ni internet.
- 2 computadoras (o 2 terminales) para ver los dos monitores serie a la vez.
- Las placas a menos de unos metros.

## Que corre en cada placa
| Placa | Comando | Que hace |
|---|---|---|
| Pulsera | `cd firmware/cogni-band && pio run -e link-test -t upload` | Sensores **simulados** (guion rapido de ~100 s) + radio ESP-NOW **real**, canal 1 |
| Robot | `cd firmware/cogni-desk && pio run -e linktest -t upload` | Solo `CommManager` + un `main` de prueba. Sin WiFi. El boton **BOOT** hace de "estoy bien" |

Despues, en cada una: `pio device monitor` (115200). Si hay varias placas conectadas: `pio device list` y `--upload-port` / `--port`.
Arrancar primero el robot (o ambos; da igual), y **reiniciar la pulsera** (boton RESET) para ver el guion desde el principio.

## Guion de la pulsera (segundos desde el arranque)
| t | Pulsera simula | Que deberia verse |
|---|---|---|
| 0-5 | arranque | Pulsera: `[radio] ESP-NOW listo | canal 1 | MAC ...`. Robot: `[comm] ESP-NOW listo | MAC ...` |
| ~5 | primera telemetria | Robot: `[comm] RX de la pulsera: tipo=1 ...` y luego `[comm] TX a la pulsera: tipo=3 ... enviado` (la HORA). Pulsera: `[comm] hora recibida del robot: ...` |
| ~10 | caida 1 | Pulsera: `POSIBLE CAIDA`. Robot: `RX tipo=2` y `>>> ¿ESTAS BIEN?`. **Pulsa BOOT en el robot** (en menos de 20 s) |
| tras BOOT | | Robot: `TX a la pulsera: tipo=4` (CANCELAR, se repite 3 veces mas). Pulsera: `[comm] CANCELAR aceptado` y `caida CANCELADA` |
| ~45 | caida 2 | Robot: `¿ESTAS BIEN?`. **No pulses nada**: a los 20 s la pulsera manda CONFIRMADA y el robot dice `Caida CONFIRMADA: 'ya avise a tu cuidador'` |
| ~70 | despertar | Pulsera: `DESPERTAR DETECTADO`; el robot lo recibe como `RX tipo=2` |

Cada 5 s el robot imprime `[test] pulsera EN LINEA | bpm=.. spo2=.. bat=..`.

## Como saber si funciona
- **Pulsera -> robot:** el robot imprime `RX de la pulsera` y los valores cambian en la linea `[test]`.
- **Robot -> pulsera:** la pulsera imprime `hora recibida del robot` y `CANCELAR aceptado`.
- Al final, en la pulsera (`[sistema] tx ok=.. fallo=..`) no debe haber `fallo` altos.

## Si no se ven
1. **Canal:** los dos usan el canal 1 (`BAND_ESPNOW_CHANNEL` y `COMM_ESPNOW_CHANNEL`). Si cambias uno, cambia el otro.
2. **Placas muy lejos o con USB-hub raro:** acercarlas.
3. **La pulsera dice `ENVIO FALLIDO`:** la radio rechazo el paquete; pega el log.
4. **El robot no imprime nada de ESP-NOW:** pega las primeras lineas de su monitor.
5. **Error de compilacion de PlatformIO:** pega el mensaje completo. Recuerda que este codigo **solo se reviso de sintaxis** y
   no se ha compilado contra el SDK real: puede haber ajustes menores.

## Lo que esta prueba NO cubre
- Entrega confiable: usa *broadcast*, que no tiene confirmacion ni reintentos de la radio.
- Interferencia con WiFi: en esta prueba el robot no se conecta al router. Con router, ESP-NOW usa el canal del router
  y la pulsera debera usar ese mismo.
- Sensores reales, bateria real, consumo de energia.
