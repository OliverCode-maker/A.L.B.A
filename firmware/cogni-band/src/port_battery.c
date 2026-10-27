#include "band_config.h"

#if !BAND_SIM

#include "band_port.h"

uint16_t port_battery_mv(void)
{
    return 0u;
}

#endif /* !BAND_SIM */
