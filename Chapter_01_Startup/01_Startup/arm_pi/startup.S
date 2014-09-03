/*! startup.S - starting point of control */

.extern	_main

.section .text

.global arch_start

arch_start:
	LDR sp, =stack
	BL _main
	B .

.section .bss
.align  4

	.lcomm _stack_, 4096
stack:
