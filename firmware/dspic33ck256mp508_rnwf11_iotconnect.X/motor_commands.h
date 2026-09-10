#ifndef MOTOR_COMMANDS_H
#define	MOTOR_COMMANDS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>

// Cloud (IoTConnect C2D) motor control entry points, defined in bldc_main.c
// and called from IOTC_RNWF11_OnCommand() in iotconnect/iotconnect_rnwf11.c.
// Kept in their own header (rather than bldc_main.h) so that including it
// does not also pull in bldc_main.h's file-scope array definitions
// (PWM_STATE1/2/3 etc.) into a second translation unit.
void MCAPP_MotorStart(void);
void MCAPP_MotorStop(void);
void MCAPP_MotorReverse(void);
void MCAPP_MotorSetSpeedPercent(uint8_t percent); // 0-100, clamped

#ifdef	__cplusplus
}
#endif

#endif	/* MOTOR_COMMANDS_H */
