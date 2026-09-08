#ifndef IOTCONNECT_RNWF11_H
#define IOTCONNECT_RNWF11_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
	uint16_t motorRunning;
	uint16_t state;
	uint16_t sector;
	uint16_t requestedSpeedRpm;
	uint16_t measuredSpeedRpm;
	int16_t requestedCurrent;
	int16_t measuredCurrent;
	int16_t dutyCycle;
	int16_t dcBusAdc;
} IOTC_RNWF11_Telemetry_t;

void IOTC_RNWF11_Initialize(void);
void IOTC_RNWF11_Tick1ms(void);
void IOTC_RNWF11_SetTelemetry(const IOTC_RNWF11_Telemetry_t *sample);
void IOTC_RNWF11_Task(void);
bool IOTC_RNWF11_IsConnected(void);

/**
 * @brief Non-blocking per-loop check for an incoming provisioning request
 *        (tools/provision_device_config.py/.ps1) on the debug console.
 *        Only reads bytes already sitting in UART1's receive buffer, so it
 *        never adds unbounded latency to the caller's loop - safe to call
 *        every iteration alongside IOTC_RNWF11_Task(). A completed
 *        provisioning exchange briefly blocks (see provisioning.c), which
 *        is fine here since motor control runs entirely in the ADC ISR,
 *        independent of this superloop.
 */
void IOTC_RNWF11_CheckProvisioning(void);

#endif
