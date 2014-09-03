#pragma once

void arch_send_message(unsigned int value, unsigned int mailbox);
unsigned int arch_read_message(unsigned int mailbox);
