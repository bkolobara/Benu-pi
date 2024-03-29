/*! Assembler macros for some processor control instructions */
#pragma once

#include <_ARCH/processor.h>

/*! halt system - stop processor or end in indefinite loop, interrupts off */
#define halt()			arch_halt()

/*! power off, if supported */
#define power_off()		arch_power_off()
