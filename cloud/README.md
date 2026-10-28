# Nube del cuidador (ThingsBoard sobre AWS)

Lo que ve y recibe el cuidador. El robot publica por MQTT el JSON de
`firmware/common/alba_thingsboard.h`; ThingsBoard lo guarda, lo muestra y genera alarmas.

```
 robot --MQTT (TLS)--> ThingsBoard (EC2) --> panel del cuidador
                          |                  '-> alarma + correo cuando una caida se confirma
                          '-- dispositivo "pulsera" (token de acceso)
```

## Mensajes (ya definidos en el codigo)

Topico `v1/devices/me/telemetry`, usuario MQTT = token del dispositivo
([formato de ThingsBoard](https://thingsboard.io/docs/paas/reference/mqtt-api/telemetry/)).

| Mensaje | Ejemplo |
|---------|---------|
| Telemetria | `{"ts":1793016003000,"values":{"bpm":62,"spo2":97,"bat":84}}` |
| Caida pendiente / confirmada / cancelada | `{"ts":...,"values":{"alert":"CAIDA_DETECTADA","confirm":"CONFIRMADA","value":4000}}` |
| Despertar | `{"ts":...,"values":{"alert":"VIGILIA_TRIGGER","confirm":"CONFIRMADA","value":89}}` |
| Bateria baja | `{"ts":...,"values":{"alert":"BATERIA_BAJA","confirm":"CONFIRMADA","value":15}}` |

`value`: caida = pico en mili-g, despertar = bpm, bateria = %. Las mediciones sin dato valido no se envian.

## Que debe mostrar y alertar (propuesta)

- **Panel:** ultimo pulso, SpO2, bateria, hora del ultimo dato (si pasan minutos sin datos, "pulsera desconectada"),
  historial de despertares y de caidas.
- **Alarma critica** (correo): `alert = CAIDA_DETECTADA` y `confirm = CONFIRMADA`.
- **Se borra:** cuando llega `confirm = CANCELADA`.
- **Aviso:** `alert = BATERIA_BAJA`.
- `PENDIENTE` **no** alerta (el robot esta preguntando al paciente; evita falsas alarmas).
- Texto fijo en el panel: "Herramienta de apoyo; no es un diagnostico clinico" (descargo de la propuesta).

En ThingsBoard esto se configura como **reglas de alarma en el perfil del dispositivo** y un **panel (dashboard)**;
los nombres exactos de los menus cambian segun la version, asi que seguir su documentacion
([instalacion con Docker](https://thingsboard.io/docs/user-guide/install/docker/),
[centro de notificaciones](https://thingsboard.io/docs/user-guide/notifications/)).

## Montarlo en AWS (resumen)

1. Cuenta AWS **del equipo** (no personal) y una **alerta de presupuesto** el primer dia.
2. Una instancia EC2 con **al menos 4 GB de RAM** (requisito de ThingsBoard para pruebas) y Docker; instalar
   ThingsBoard CE con la guia de Docker.
3. *Security group*: abrir solo lo necesario. SSH (22) solo desde tu IP; el panel web; y MQTT. **El 1883 manda el token
   sin cifrar**: para el robot real usar el 8883 con TLS (guia "MQTT over SSL" de ThingsBoard). Mientras se prueba con
   el simulador, limitar el 1883 a tu IP.
4. En ThingsBoard: crear el dispositivo `pulsera`, copiar su token, crear la alarma y el panel de arriba.
5. Guardar el token **fuera del repositorio** (variable de entorno `TB_TOKEN`).

## Probarlo sin pulsera ni robot

```bash
pip install paho-mqtt
export TB_HOST=<ip o dominio del servidor>
export TB_TOKEN=<token del dispositivo>
python3 cloud/simulador_robot.py --dry-run              # solo imprime lo que publicaria
python3 cloud/simulador_robot.py --port 8883 --tls      # escenario completo (~2 min)
python3 cloud/simulador_robot.py --solo caida --rapido  # solo la caida, 5 veces mas rapido
```
Escenarios: `normal`, `caida`, `cancelada`, `bateria`, `despertar`. Pruebas del formato:
`cd cloud && python3 -m unittest test_simulador`.

**Estado:** el simulador y el formato estan probados contra un broker MQTT local (sin TLS) y contra las pruebas
en C. Falta probarlos contra un ThingsBoard real y con TLS.
