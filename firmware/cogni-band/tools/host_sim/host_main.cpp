/* Arranque en el PC: crea las mismas tareas que setup() y corre N segundos. */
#include <stdio.h>
#include <stdlib.h>

#include "band_config.h"
#include "band_os.h"
#include "band_port.h"
#include "band_tasks.h"

static void task_stopper(void *arg)
{
    const unsigned seconds = (unsigned)(size_t)arg;
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000u));
    const band_stats_t s = band_stats_snapshot();
    port_log("== fin de la simulacion (%us) ==", seconds);
    port_log("caidas: candidatas=%lu confirmadas=%lu canceladas=%lu rechazadas=%lu | despertares=%lu",
             (unsigned long)s.falls_candidate, (unsigned long)s.falls_confirmed,
             (unsigned long)s.falls_cancelled, (unsigned long)s.falls_rejected,
             (unsigned long)s.wake_triggers);
    port_log("radio: tx ok=%lu fallo=%lu descartados=%lu | rx aceptados=%lu ignorados=%lu descartados=%lu",
             (unsigned long)s.tx_sent, (unsigned long)s.tx_failed, (unsigned long)s.tx_dropped,
             (unsigned long)s.rx_accepted, (unsigned long)s.rx_ignored, (unsigned long)s.rx_dropped);
    exit(0);
}

int main(int argc, char **argv)
{
    const unsigned seconds = (argc > 1) ? (unsigned)atoi(argv[1]) : 100u;
    port_log("ALBA cogni-band (PC, FreeRTOS POSIX) | simulacion %s",
             BAND_SIM_FAST ? "rapida" : "tiempo real");

    if (!band_tasks_start()) { printf("ERROR: no se pudieron crear las tareas\n"); return 1; }
    port_start();
    xTaskCreate(task_stopper, "stopper", BAND_STACK(4096u), (void *)(size_t)seconds, 1, nullptr);
    vTaskStartScheduler();
    return 1;
}
