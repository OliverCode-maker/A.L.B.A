#!/usr/bin/env python3
"""
Simulador del ROBOT para probar la nube (ThingsBoard) sin pulsera ni robot.

Publica por MQTT los mismos mensajes que publicara el robot real
(firmware/common/alba_thingsboard.h): telemetria y eventos de la pulsera.

Uso:
    pip install paho-mqtt
    export TB_HOST=mi-servidor.ejemplo.com      # servidor ThingsBoard
    export TB_TOKEN=xxxxxxxx                    # token de acceso del dispositivo "pulsera"
    python3 simulador_robot.py                  # escenario completo (~2 min)
    python3 simulador_robot.py --dry-run        # solo imprime el JSON, sin red

Opciones utiles:
    --port 8883 --tls    conexion cifrada (recomendado; el puerto 1883 manda el token sin cifrar)
    --solo caida         solo el escenario de caida confirmada
    --rapido             espera 1/5 del tiempo entre mensajes

IMPORTANTE: el token NO va en el codigo ni en el repositorio (es publico). Se lee
de la variable de entorno TB_TOKEN.
"""
import argparse
import json
import os
import sys
import time

TOPIC = "v1/devices/me/telemetry"      # igual que ALBA_TB_TOPIC en alba_thingsboard.h


def telemetria(bpm=None, spo2=None, bat=None, ts_ms=None):
    """Como alba_telemetry_to_tb(): las claves sin dato valido se omiten."""
    values = {}
    if bpm is not None:
        values["bpm"] = bpm
    if spo2 is not None:
        values["spo2"] = spo2
    if bat is not None:
        values["bat"] = bat
    if not values:
        raise ValueError("no hay ninguna medicion valida que enviar")
    return _con_ts(values, ts_ms)


def evento(alerta, confirm, valor, ts_ms=None):
    """Como alba_event_to_tb(). value: caida = pico en mili-g, vigilia = bpm, bateria = %."""
    if alerta not in ("CAIDA_DETECTADA", "VIGILIA_TRIGGER", "BATERIA_BAJA"):
        raise ValueError("alerta desconocida: %s" % alerta)
    if confirm not in ("PENDIENTE", "CONFIRMADA", "CANCELADA"):
        raise ValueError("confirmacion desconocida: %s" % confirm)
    return _con_ts({"alert": alerta, "confirm": confirm, "value": valor}, ts_ms)


def _con_ts(values, ts_ms):
    msg = {}
    if ts_ms:
        msg["ts"] = ts_ms
    msg["values"] = values
    return json.dumps(msg, separators=(",", ":"))       # sin espacios, como el codigo C


def escenario(solo=None):
    """Lista de (segundos_de_espera, JSON). Cada elemento es un mensaje que el robot publicaria."""
    pasos = []

    def tel(espera, bpm, bat, spo2=97):
        pasos.append((espera, lambda ts: telemetria(bpm, spo2, bat, ts)))

    def ev(espera, alerta, confirm, valor):
        pasos.append((espera, lambda ts: evento(alerta, confirm, valor, ts)))

    if solo in (None, "normal"):
        for i in range(4):
            tel(5, 56 + i % 3, 80 - i)
    if solo in (None, "caida"):                           # caida sin respuesta: llega al cuidador
        ev(5, "CAIDA_DETECTADA", "PENDIENTE", 4000)
        tel(10, 62, 78)
        ev(10, "CAIDA_DETECTADA", "CONFIRMADA", 4000)
    if solo in (None, "cancelada"):                       # el paciente dice "estoy bien": no debe alertar
        tel(10, 60, 77)
        ev(5, "CAIDA_DETECTADA", "PENDIENTE", 3900)
        ev(4, "CAIDA_DETECTADA", "CANCELADA", 3900)
    if solo in (None, "bateria"):
        tel(10, 58, 16)
        ev(5, "BATERIA_BAJA", "CONFIRMADA", 15)
    if solo in (None, "despertar"):
        for bpm in (56, 66, 78, 88, 89):
            tel(5, bpm, 14)
        ev(2, "VIGILIA_TRIGGER", "CONFIRMADA", 89)
    return pasos


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true", help="solo imprimir, sin conectar")
    ap.add_argument("--host", default=os.environ.get("TB_HOST", "localhost"))
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--tls", action="store_true", help="MQTT sobre TLS (usar con --port 8883)")
    ap.add_argument("--solo", choices=["normal", "caida", "cancelada", "bateria", "despertar"])
    ap.add_argument("--rapido", action="store_true", help="esperas 5 veces mas cortas")
    args = ap.parse_args()

    token = os.environ.get("TB_TOKEN", "")
    cliente = None
    if not args.dry_run:
        if not token:
            sys.exit("Falta la variable de entorno TB_TOKEN (token del dispositivo en ThingsBoard).")
        try:
            import paho.mqtt.client as mqtt
        except ImportError:
            sys.exit("Falta paho-mqtt:  pip install paho-mqtt")
        try:
            cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)       # paho >= 2
        except AttributeError:
            cliente = mqtt.Client()                                        # paho 1.x
        cliente.username_pw_set(token)                                    # ThingsBoard: el token es el usuario
        if args.tls:
            cliente.tls_set()
        cliente.connect(args.host, args.port, keepalive=60)
        cliente.loop_start()
        print("Conectado a %s:%d" % (args.host, args.port))

    for espera, armar in escenario(args.solo):
        time.sleep(0 if args.dry_run else espera / (5 if args.rapido else 1))
        payload = armar(int(time.time() * 1000))
        print("-> %s  %s" % (TOPIC, payload))
        if cliente is not None:
            info = cliente.publish(TOPIC, payload, qos=1)
            info.wait_for_publish(timeout=10)

    if cliente is not None:
        cliente.loop_stop()
        cliente.disconnect()


if __name__ == "__main__":
    main()
