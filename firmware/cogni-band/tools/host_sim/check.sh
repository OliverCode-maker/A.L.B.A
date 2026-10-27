#!/bin/bash
# Prueba de integracion en el PC: corre el guion rapido (~100 s) con FreeRTOS
# real y comprueba que paso lo que tenia que pasar. Sale con 0 si todo bien.
#   tools/host_sim/check.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

"$HERE/run.sh" 100 > "$LOG" 2>&1 || { echo "FALLO: no compila o termina con error"; tail -20 "$LOG"; exit 1; }

fail=0
expect() {   # expect "descripcion" "patron (grep -E)"
  if grep -Eq "$2" "$LOG"; then echo "  ok   $1"; else echo "  FALLA $1   (esperaba: $2)"; fail=1; fi
}

echo "Verificando guion..."
expect "el robot da la hora y la pulsera la acepta"       "hora recibida del robot: .*hora local 7 \(ok\)"
expect "caida 1: llega PENDIENTE al robot"                 'CAIDA_DETECTADA.*"peak":4000,"confirm":"PENDIENTE"'
expect "caida 1: sin cancelar llega CONFIRMADA"            'CAIDA_DETECTADA.*"confirm":"CONFIRMADA"'
expect "caida 2: el robot cancela y la pulsera acepta"     "CANCELAR aceptado"
expect "caida 2: llega CANCELADA al robot"                 'CAIDA_DETECTADA.*"confirm":"CANCELADA"'
expect "el CANCELAR repetido se ignora"                    "CANCELAR ignorado"
expect "bateria baja avisa"                                'BATERIA_BAJA.*"peak":15'
expect "despertar detectado"                               'VIGILIA_TRIGGER'
expect "caidas: 2 candidatas, 1 confirmada, 1 cancelada"   "candidatas=2 confirmadas=1 canceladas=1 rechazadas=0 \| despertares=1"
expect "radio sin fallos ni descartes"                     "tx ok=[0-9]+ fallo=0 descartados=0 \| rx aceptados=2 ignorados=1 descartados=0"
expect "telemetria con pulso valido"                '"type":"telemetry".*"bpm":5[0-9]'
[ "$(grep -c 'BATERIA_BAJA","peak' "$LOG")" -eq 1 ] || { echo "  FALLA la bateria baja debe avisar exactamente una vez"; fail=1; }
[ "$(grep -c 'VIGILIA_TRIGGER' "$LOG")" -eq 1 ] || { echo "  FALLA el despertar debe avisar exactamente una vez"; fail=1; }

if [ $fail -eq 0 ]; then echo "TODO BIEN"; else echo "HAY FALLOS (log completo abajo)"; cat "$LOG"; fi
exit $fail
