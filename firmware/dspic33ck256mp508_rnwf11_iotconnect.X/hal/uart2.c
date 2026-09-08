#include <xc.h>
#include <stdbool.h>
#include <stdint.h>
#include "clock.h"
#include "uart2.h"

#define UART2_RX_BUFFER_SIZE 512U

static volatile uint8_t rxBuffer[UART2_RX_BUFFER_SIZE];
static volatile uint16_t rxHead;
static volatile uint16_t rxTail;
static volatile bool rxOverflow;

void UART2_Initialize(uint32_t baud)
{
    _U2RXIE = 0;
    _U2RXIF = 0;
    _U2TXIE = 0;
    _U2TXIF = 0;

    U2MODE = 0;
    U2MODEH = 0;
    U2STA = 0;
    U2STAH = 0;

    /* 8-N-1, high-speed BRG so 230400 lands within 0.5% at FCY = 100 MHz. */
    U2MODEbits.MOD = 0;
    U2MODEbits.BRGH = 1;
    U2BRG = (uint16_t)((FCY / (4UL * baud)) - 1UL);
    U2BRGH = 0;

    U2STAHbits.URXISEL = 0;

    rxHead = 0;
    rxTail = 0;
    rxOverflow = false;

    U2MODEbits.UARTEN = 1;
    U2MODEbits.UTXEN = 1;
    U2MODEbits.URXEN = 1;

    /* Below the motor control ADC interrupt so commutation is never delayed. */
    _U2RXIP = 2;
    _U2RXIF = 0;
    _U2RXIE = 1;
}

static void UART2_DrainFifo(void)
{
    while (!U2STAHbits.URXBE)
    {
        uint8_t data = (uint8_t)U2RXREG;
        uint16_t next = (uint16_t)((rxHead + 1U) % UART2_RX_BUFFER_SIZE);
        if (next != rxTail)
        {
            rxBuffer[rxHead] = data;
            rxHead = next;
        }
        else
        {
            rxOverflow = true;
        }
    }
    if (U2STAbits.OERR)
    {
        U2STAbits.OERR = 0;
        rxOverflow = true;
    }
}

void __attribute__((__interrupt__, no_auto_psv)) _U2RXInterrupt(void)
{
    /* Clear before draining so a byte arriving mid-drain re-triggers. */
    _U2RXIF = 0;
    UART2_DrainFifo();
}

void UART2_Write(uint8_t data)
{
    while (U2STAHbits.UTXBF)
    {
    }
    U2TXREG = data;
}

bool UART2_IsTransmitComplete(void)
{
    return U2STAbits.TRMT;
}

bool UART2_IsOverrun(void)
{
    return rxOverflow;
}

void UART2_ClearOverrun(void)
{
    rxOverflow = false;
}

bool UART2_IsReceiveDataAvailable(void)
{
    if (rxHead == rxTail)
    {
        /* Fallback in case an interrupt was missed. */
        _U2RXIE = 0;
        UART2_DrainFifo();
        _U2RXIE = 1;
    }
    return rxHead != rxTail;
}

uint8_t UART2_Read(void)
{
    uint8_t data = rxBuffer[rxTail];
    rxTail = (uint16_t)((rxTail + 1U) % UART2_RX_BUFFER_SIZE);
    return data;
}

void UART2_ReceiveFlush(void)
{
    _U2RXIE = 0;
    while (!U2STAHbits.URXBE)
    {
        (void)U2RXREG;
    }
    U2STAbits.OERR = 0;
    rxHead = 0;
    rxTail = 0;
    rxOverflow = false;
    _U2RXIF = 0;
    _U2RXIE = 1;
}
