#include <time.h>

// iotc-c-lib (core/src/iotcl_util.c) calls the standard time() to timestamp
// telemetry when a time_fn is configured - XC16's own time() implementation
// hard-wires itself to classic Timer2 (T2CON/TMR2) as its hardware timebase,
// which this device doesn't have (this board's timers are SCCP-based, see
// hal/sccp.c), so the standard library version fails to link. This project
// never configures a time_fn (no telemetry field needs a real timestamp),
// so a fixed-epoch stub is sufficient - it just needs to exist so the
// linker never has to pull in the unusable library implementation.
time_t time(time_t *out)
{
    if (out != NULL)
    {
        *out = 0;
    }
    return 0;
}
