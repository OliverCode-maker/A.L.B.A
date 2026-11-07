#ifndef COMM_MANAGER_H
#define COMM_MANAGER_H

/**
 * CommManager: todas las comunicaciones del robot.
 *   - ESP-NOW  <-> pulsera (recibe telemetria y alertas; manda HORA y CANCELAR)
 *   - WiFi + NTP (hora real) 
 *   - MQTT por TLS -> ThingsBoard (el panel del cuidador)
 * La logica vive en lib/comm_core (probada en el PC). Este archivo solo la conecta
 * con el hardware.
 *
 * Uso desde main.cpp (los tres primeros ya los llama main.cpp):
 *     initCommunication();                  // en setup()
 *     procesarComunicacionesBackground();   // en cada loop(), no bloquea
 *     sincronizarDatosDashboardBackground();// en cada loop(); puede tardar unos segundos
 *                                           //   cuando intenta reconectar al servidor
 * Para la interfaz:
 *     commPromptCaidaActivo()   -> mostrar "¿Estas bien?"  (y el boton OK llama commBotonEstoyBien())
 *     commTomarAyudaAvisada()   -> true UNA vez: "ya avise a tu cuidador"
 *     commHayDatosPulsera(), commBpm(), commSpo2(), commBateria(), commPulseraOnline()
 *     commPublicarAmbiente(...) -> temperatura y humedad del cuarto hacia la nube
 *
 * Credenciales: crear include/comm_secrets.h a partir de comm_secrets.example.h.
 * Ese archivo NO se sube al repositorio (esta en .gitignore).
 */

#include <stdbool.h>
#include <stdint.h>

void initCommunication();
void procesarComunicacionesBackground();
void sincronizarDatosDashboardBackground();

/** Llamar cuando el paciente pulsa "estoy bien" mientras se muestra la pregunta. */
void commBotonEstoyBien();
bool commPromptCaidaActivo();
bool commTomarAyudaAvisada();

bool    commHayDatosPulsera();
uint8_t commBpm();          // 0 = sin medicion valida
uint8_t commSpo2();         // 0xFF = sin medicion valida
uint8_t commBateria();      // 0xFF = la pulsera no mide su bateria
bool    commPulseraOnline();

// Estado de la nube (para depurar y para la interfaz)
bool     commWifiConectado();
bool     commNubeConectada();
uint32_t commMensajesPublicados();   // cuantos JSON se han enviado a ThingsBoard
uint32_t commMensajesEnCola();       // cuantos esperan (sin WiFi/MQTT o con el servidor caido)

/** Temperatura (decimas de grado: 235 = 23.5 C) y humedad (%). Se envia como mucho una vez por minuto.
 *  Usar INT16_MIN / 0xFF si el sensor no tiene lectura. */
void commPublicarAmbiente(int16_t temp_c_x10, uint8_t hum_pct);

/** El paciente confirmo la toma de su medicamento (lo llama main.cpp via registrarTomaEnDashboard). */
void registrarTomaEnDashboard();

#endif
