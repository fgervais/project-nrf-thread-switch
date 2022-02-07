#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
#define LED0    DT_GPIO_LABEL(LED0_NODE, gpios)
#define PIN     DT_GPIO_PIN(LED0_NODE, gpios)
#define FLAGS   DT_GPIO_FLAGS(LED0_NODE, gpios)
#else
/* A build error here means your board isn't set up to blink an LED. */
#error "Unsupported board: led0 devicetree alias is not defined"
#define LED0    ""
#define PIN     0
#define FLAGS   0
#endif

#define SW0_NODE	DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif


static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;


// static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
// 						     {0});


static bool led_is_on = true;
void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	// printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	// gpio_pin_set_dt(&led, (int)led_is_on);
	led_is_on = !led_is_on;
}

void main(void)
{
	int ret;
	// const struct device *cons = device_get_binding(CONSOLE_LABEL);

	if (!device_is_ready(button.port)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button.port->name, button.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_LEVEL_LOW);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", button.port->name, button.pin);

	// if (led.port && !device_is_ready(led.port)) {
	// 	printk("Error %d: LED device %s is not ready; ignoring it\n",
	// 	       ret, led.port->name);
	// 	led.port = NULL;
	// }
	// if (led.port) {
	// 	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
	// 	if (ret != 0) {
	// 		printk("Error %d: failed to configure LED device %s pin %d\n",
	// 		       ret, led.port->name, led.pin);
	// 		led.port = NULL;
	// 	} else {
	// 		printk("Set up LED at %s pin %d\n", led.port->name, led.pin);
	// 	}
	// }

	// pm_device_state_set(cons, PM_DEVICE_STATE_SUSPENDED);


	for (ret = 0; ret < 5; ret++) {
		led_is_on = !led_is_on;
		k_sleep(K_SECONDS(1));
	}
}
