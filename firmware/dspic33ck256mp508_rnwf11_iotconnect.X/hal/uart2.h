#ifndef __UART2_H
#define __UART2_H

#include <stdbool.h>
#include <stdint.h>

/* UART2 carries the RNWF11 AT link on the mikroBUS B header. */

void UART2_Initialize(uint32_t baud);
void UART2_Write(uint8_t data);
bool UART2_IsTransmitComplete(void);

/* Latches after a receive overflow and blocks reception until cleared. */
bool UART2_IsOverrun(void);
void UART2_ClearOverrun(void);
bool UART2_IsReceiveDataAvailable(void);
uint8_t UART2_Read(void);
void UART2_ReceiveFlush(void);

#endif /* __UART2_H */
