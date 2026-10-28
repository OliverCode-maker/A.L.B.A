"""Comprueba que el simulador arma EXACTAMENTE el mismo JSON que alba_thingsboard.h.
Las cadenas esperadas son las mismas de firmware/cogni-band/test/test_thingsboard.
Correr con:  python3 -m unittest cloud/test_simulador.py   (desde la raiz del repo)"""
import json
import unittest

import simulador_robot as s


class TestFormato(unittest.TestCase):
    def test_telemetria_igual_que_en_c(self):
        self.assertEqual(s.telemetria(62, 97, 84, 1793016003000),
                         '{"ts":1793016003000,"values":{"bpm":62,"spo2":97,"bat":84}}')

    def test_sin_ts_se_omite(self):
        self.assertEqual(s.telemetria(62, 97, 84), '{"values":{"bpm":62,"spo2":97,"bat":84}}')

    def test_claves_invalidas_se_omiten(self):
        self.assertEqual(s.telemetria(None, 97, 84, 5), '{"ts":5,"values":{"spo2":97,"bat":84}}')
        self.assertEqual(s.telemetria(62, None, None, 5), '{"ts":5,"values":{"bpm":62}}')

    def test_sin_nada_valido_no_se_envia(self):
        with self.assertRaises(ValueError):
            s.telemetria()

    def test_evento_igual_que_en_c(self):
        self.assertEqual(
            s.evento("CAIDA_DETECTADA", "CONFIRMADA", 3850, 1793016013270),
            '{"ts":1793016013270,"values":{"alert":"CAIDA_DETECTADA","confirm":"CONFIRMADA","value":3850}}')

    def test_evento_invalido(self):
        with self.assertRaises(ValueError):
            s.evento("OTRA", "CONFIRMADA", 1)
        with self.assertRaises(ValueError):
            s.evento("BATERIA_BAJA", "TAL_VEZ", 1)

    def test_escenario_completo_es_json_valido_y_cubre_todo(self):
        textos = [armar(1793016000000) for _, armar in s.escenario()]
        vistos = set()
        for t in textos:
            v = json.loads(t)["values"]
            vistos.add(v.get("alert", "telemetria"))
            vistos.add(v.get("confirm", "-"))
        for esperado in ("telemetria", "CAIDA_DETECTADA", "BATERIA_BAJA", "VIGILIA_TRIGGER",
                         "PENDIENTE", "CONFIRMADA", "CANCELADA"):
            self.assertIn(esperado, vistos)

    def test_topico(self):
        self.assertEqual(s.TOPIC, "v1/devices/me/telemetry")


if __name__ == "__main__":
    unittest.main()
