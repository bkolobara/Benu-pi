
#define READ_MAIL 0x2000B880
#define PULL_MAIL 0x2000B890
#define SENDER_INFO 0x2000B894
#define STATUS 0x2000B898
#define CONFIG_MAIL 0x2000B89C
#define SEND_MAIL 0x2000B8A0

#define TOP_BIT 0x80000000
#define SECOND_TOP_BIT 0x40000000

#define TOP_28_BITS 0xfffffff0
#define LAST_4_BITS 0b1111

void arch_wait_for_send() {
	volatile unsigned int *status = (unsigned int *) STATUS;
	while (*status & TOP_BIT) {}
}

void arch_wait_for_read() {
	volatile unsigned int *status = (unsigned int *) STATUS;
	while (*status & SECOND_TOP_BIT) {}
}

void arch_send_message(unsigned int value, unsigned int mailbox)
{
	volatile unsigned int *send_mail = (unsigned int *) SEND_MAIL;
	unsigned int message = value + mailbox;
	arch_wait_for_send();
	*send_mail = message;
}

unsigned int arch_read_message(unsigned int mailbox)
{
	unsigned int message;
	unsigned int value;
	unsigned int read_mailbox;
	volatile unsigned int *read_mail = (unsigned int *) READ_MAIL;

	while (1) {
		arch_wait_for_read();

		message = *read_mail;
		value = message & TOP_28_BITS;
		read_mailbox = message & LAST_4_BITS;
		if (read_mailbox == mailbox) {
			break;
		}
	}
	return value;
}
