#include <xc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "clock.h"
#include "uart1.h"
#include "debug_console.h"

#define DEBUG_LINE_MAX 400

void DEBUG_Initialize(void)
{
    UART1_InterruptReceiveDisable();
    UART1_InterruptReceiveFlagClear();
    UART1_InterruptTransmitDisable();
    UART1_InterruptTransmitFlagClear();
    UART1_Initialize();
    UART1_SpeedModeStandard();
    UART1_BaudRateDividerSet((uint16_t)((FCY / (16UL * DEBUG_CONSOLE_BAUD)) - 1UL));
    UART1_ModuleEnable();
}

void DEBUG_Printf(const char *format, ...)
{
    char line[DEBUG_LINE_MAX];
    va_list args;

    va_start(args, format);
    int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    if (length < 0)
    {
        return;
    }
    if (length > (int)(sizeof(line) - 1))
    {
        length = (int)(sizeof(line) - 1);
    }

    for (int i = 0; i < length; i++)
    {
        while (U1STAHbits.UTXBF)
        {
        }
        U1TXREG = (uint8_t)line[i];
    }
}

void DEBUG_Flush(void)
{
    while (!U1STAbits.TRMT)
    {
    }
}
