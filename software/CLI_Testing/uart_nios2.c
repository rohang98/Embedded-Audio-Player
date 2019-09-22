/****************************************************************************
*  Copyright (C) 2012 by Michael Fischer.
*
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without 
*  modification, are permitted provided that the following conditions 
*  are met:
*  
*  1. Redistributions of source code must retain the above copyright 
*     notice, this list of conditions and the following disclaimer.
*  2. Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the 
*     documentation and/or other materials provided with the distribution.
*  3. Neither the name of the author nor the names of its contributors may 
*     be used to endorse or promote products derived from this software 
*     without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL 
*  THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS 
*  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED 
*  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
*  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF 
*  THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
*  SUCH DAMAGE.
*
****************************************************************************
*  History:
*
*  23.08.2012  mifi  First Version
****************************************************************************/
#define __UART_C__

/*=========================================================================*/
/*  Includes                                                               */
/*=========================================================================*/
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "uart.h"

#include "system.h"
#include "io.h"
#include "sys/alt_irq.h"
#include "altera_avalon_uart.h"
#include "altera_avalon_uart_regs.h"

/*=========================================================================*/
/*  DEFINE: All Structures and Common Constants                            */
/*=========================================================================*/
#define SETBIT(_a) (1<<_a)

/*=========================================================================*/
/*  DEFINE: Definition of all local Data                                   */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Definition of all local Procedures                             */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: All code exported                                              */
/*=========================================================================*/
/***************************************************************************/
/*  uart0_init                                                             */
/*                                                                         */
/*  In    : Baudrate                                                       */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
void uart0_init (unsigned long Baudrate)
{
//   uint32_t Divisor;
//
//   /*
//    * Disable interrupts which was still enabled
//    * from the Niso II system at startup.
//    */
//   //alt_ic_irq_disable(0, UART_IRQ);
//   IOWR_ALTERA_AVALON_UART_CONTROL(UART_BASE, 0);
//
//   /*
//    * Set baudrate
//    */
//   Divisor = (((UART_FREQ*10) / Baudrate) + 5) / 10;
//   IOWR_ALTERA_AVALON_UART_DIVISOR(UART_BASE, Divisor);
} /* uart0_init */


/***************************************************************************/
/*  uart0_put                                                              */
/*                                                                         */
/*  Send a character over the uart0 channel,                               */
/*  check before if no character is in the US_THR.                         */
/*                                                                         */
/*  In    : Data                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
void uart0_put (unsigned char ch)
{
//   uint32_t status;
//
//   if (ch == '\n')
//   {
//      status = IORD_ALTERA_AVALON_UART_STATUS(UART_BASE);
//      while ((status & ALTERA_AVALON_UART_STATUS_TRDY_MSK) == 0)
//      {
//         status = IORD_ALTERA_AVALON_UART_STATUS(UART_BASE);
//      }
//      IOWR_ALTERA_AVALON_UART_TXDATA(UART_BASE, '\r');
//   }
//
//   status = IORD_ALTERA_AVALON_UART_STATUS(UART_BASE);
//   while ((status & ALTERA_AVALON_UART_STATUS_TRDY_MSK) == 0)
//   {
//      status = IORD_ALTERA_AVALON_UART_STATUS(UART_BASE);
//   }
//   IOWR_ALTERA_AVALON_UART_TXDATA(UART_BASE, ch);

		printf("%c",ch);



} /* uart0_put */
 
/***************************************************************************/
/*  uart0_get                                                              */
/*                                                                         */
/*  Return a character if available from the given device.                 */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: Data                                                           */
/***************************************************************************/
unsigned char uart0_get (void)
{
   char ch;

//   while ((IORD_ALTERA_AVALON_UART_STATUS(UART_BASE) & ALTERA_AVALON_UART_STATUS_RRDY_MSK) == 0)
//   {
//      asm ("nop");
//   }
//   ch = IORD_ALTERA_AVALON_UART_RXDATA(UART_BASE);


//   volatile int * JTAG_UART_ptr = (int *)JTAG_UART_0_BASE; // JTAG UART address
//   int data;
//   data = *(JTAG_UART_ptr); // read the JTAG_UART data register
//   while (!(data & 0x00008000))
//   {
//	   data = *(JTAG_UART_ptr);// check RVALID to see if there is new data
//   }
//
//
//
//
//   //printf("%d",(char) data & 0xFF);
//
//      return ((char) data & 0xFF);


   ch = getc(stdin);
   if (ch == 10) ch = 13;

   return(ch);

} /* uart0_get */

/*** EOF ***/
