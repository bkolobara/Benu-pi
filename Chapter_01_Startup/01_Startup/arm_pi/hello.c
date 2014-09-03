/* Make the LED light go on and off */

#define GPIO_CONTROLLER	0x20200000
#define GPIO_OFF_OFFSET	40
#define GPIO_ON_OFFSET	28

#define WAIT_COUNT	0x003F0000

void set_gpio_pin_for_write(unsigned int gpio_pin)
{
	unsigned int gpio_group = gpio_pin / 10;
	unsigned int gpio_pin_in_group = (gpio_pin % 10) * 3;
	volatile unsigned int *gpio_group_controller =
		(unsigned int *) (GPIO_CONTROLLER + (4 * gpio_group));

	*gpio_group_controller = 1 << gpio_pin_in_group;
}

void turn_gpio_pin_on(unsigned int gpio_pin)
{
	volatile unsigned int *gpio_on_controller =
		(unsigned int *) (GPIO_CONTROLLER + GPIO_ON_OFFSET);
	*gpio_on_controller = 1 << gpio_pin;
}

void turn_gpio_pin_off(unsigned int gpio_pin)
{
	volatile unsigned int *gpio_off_controller =
		(unsigned int *) (GPIO_CONTROLLER + GPIO_OFF_OFFSET);
	*gpio_off_controller = 1 << gpio_pin;
}

void _main()
{
	set_gpio_pin_for_write(16);
	turn_gpio_pin_off(16);
}
