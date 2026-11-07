enum EstadoSistema {
    ESTADO_INICIO,      // Splash screen (A.L.B.A.)
    ESTADO_OJOS_IDLE,   // Ojos parpadeando (pantalla principal en reposo)
    ESTADO_MENU,        // Menú minimalista interactivo
    ESTADO_MODO_JUEGO,  // Ejecutando el Cogni-Game de estimulación
    ESTADO_ALERTA_MED   // Interrupción por recordatorio de pastillas
};
#include <Arduino.h>
#include "DisplaysManager.h"
#include "ButtonsManager.h"
#include "SensorManager.h"
#include "CommManager.h"
#include "StorageManager.h"
#include "ReminderSystem.h"
#include "CognitiveGame.h"

// Variable global de estado
EstadoSistema estadoActual = ESTADO_INICIO;

void setup() {
    Serial.begin(115200);

    // 1. Inicializar memorias y almacenamiento (EEPROM)
    initStorage(); 
    
    // 2. Inicializar hardware base (Pantallas, Botones, Sensores)
    initDisplays();
    initButtons();
    initSensors();

    // 3. Inicializar comunicaciones (Wi-Fi, ESP-NOW para Cogni-Band)
    initCommunication(); 

    // 4. Mostrar presentación de encendido
    mostrarSplashScreenALBA();
    delay(3000); 

    estadoActual = ESTADO_OJOS_IDLE;
}

void loop() {
    // --- PASO A PASO NO BLOQUEANTE DEL SISTEMA ---

    // A. Actualizar entradas físicas (botones) constantemente
    actualizarBotones();

    // B. Escuchar comunicaciones en segundo plano (Datos de Cogni-Band por ESP-NOW)
    procesarComunicacionesBackground();

    // C. Evaluar si hay un recordatorio de medicamentos pendiente (Prioridad alta)
    if (verificarAlarmasMedicamentos()) {
        estadoActual = ESTADO_ALERTA_MED;
    }

    // D. Gestionar comportamiento según el estado actual del sistema
    switch (estadoActual) {
        
        case ESTADO_OJOS_IDLE:
            // Animar ojos parpadeando sin bloquear el loop
            actualizarAnimacionOjosNoBloqueante();
            
            // Si el usuario presiona el botón "OK", entra al menú principal
            if (leerBotonOKPresionado()) {
                estadoActual = ESTADO_MENU;
                reiniciarSeleccionMenu();
            }
            break;

        case ESTADO_MENU:
            // Navegar por el menú minimalista: 
            // [Temperatura] [Humedad] [Datos Cogni-Band] [Hora] [Cogni-Game]
            manejarNavegacionMenu();

            // Si selecciona "Cogni-Game", cambiamos al modo juego
            if (usuarioSeleccionoCogniGame()) {
                estadoActual = ESTADO_MODO_JUEGO;
                iniciarCogniGame();
            }
            // Si el usuario decide salir o pasa tiempo inactivo, vuelve a los ojos
            else if (usuarioDeseaSalirMenu()) {
                estadoActual = ESTADO_OJOS_IDLE;
            }
            break;

        case ESTADO_MODO_JUEGO:
            // Ejecutar la lógica de los mini juegos de memoria
            ejecutarLogicaCogniGame();

            // Al terminar el juego, regresa al menú
            if (cogniGameHaFinalizado()) {
                estadoActual = ESTADO_MENU;
            }
            break;

        case ESTADO_ALERTA_MED:
            // Mostrar interfaz de advertencia de pastillas en las OLED
            mostrarAlertaMedicamentosUI();

            // Si el usuario confirma la toma con el botón, la alarma se apaga y vuelve a IDLE
            if (usuarioConfirmoTomaMedicamento()) {
                registrarTomaEnDashboard(); // Envía el evento al dashboard en línea
                estadoActual = ESTADO_OJOS_IDLE;
            }
            break;

        default:
            break;
    }

    // E. Sincronizar datos periódicamente con el Dashboard en línea (vía Wi-Fi)
    sincronizarDatosDashboardBackground();
}