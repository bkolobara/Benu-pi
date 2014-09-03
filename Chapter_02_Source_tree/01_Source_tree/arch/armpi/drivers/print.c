/*! Print on console using ARM PrimeCell UART (PL011) [very simple mode!] */

#include <types/basic.h>
#include <_ARCH/config.h>
#include <arch/gpu.h>

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))

#define UART0_FR	(UART0_BASE + 0x18)
#define UART0_DR	(UART0_BASE + 0x00)


//char* font = &font_addr;

/*! Init console */
int arch_console_init ()
{
	arch_gpu_init();
	return 0;
}

static void arch_putchar ( char c )
{
	volatile unsigned int *uart_dr = (unsigned int *) UART0_DR;
	volatile unsigned int *uart_fr = (unsigned int *) UART0_FR;

	/* Wait for UART to become ready to transmit */
	while ( (*uart_fr) & (1 << 5) )
		;

	/* Transmit char */
	*uart_dr = (unsigned int) c;

}
/*!
 * Print single word on console in new line
 * \param word Word to print (word must be shorter than 80 characters)
 */
int arch_console_print_word ( char *word )
{

	if ( word == NULL )
		return -1;


	while ( *word != '\0' ) /* Loop until end of string */
		arch_putchar ( *word++ );

	arch_putchar ( '\n' );

	return 0;
}
