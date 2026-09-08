#ifndef __DEBUG_CONSOLE_H
#define __DEBUG_CONSOLE_H

/*
 * Blocking text console on UART1 (RD13/RD14), reachable through the PKoB4
 * virtual COM port on connector J13. Not safe to call from an ISR.
 */

#define DEBUG_CONSOLE_BAUD 115200UL

void DEBUG_Initialize(void);
void DEBUG_Printf(const char *format, ...);

/* Blocks until the last character has left the shift register. */
void DEBUG_Flush(void);

#endif /* __DEBUG_CONSOLE_H */
