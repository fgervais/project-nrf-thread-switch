#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <event_manager.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

// #include <net/openthread.h>
// #include <openthread/thread.h>

#include <net/socket.h>
#include <net/mqtt.h>
#include <random/rand32.h>

#define MODULE main
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

#define CONSOLE_LABEL DT_LABEL(DT_CHOSEN(zephyr_console))

/* The devicetree node identifier for the "led0" alias. */
// #define LED0_NODE DT_ALIAS(led0)

// #if DT_NODE_HAS_STATUS(LED0_NODE, okay)
// #define LED0    DT_GPIO_LABEL(LED0_NODE, gpios)
// #define PIN     DT_GPIO_PIN(LED0_NODE, gpios)
// #define FLAGS   DT_GPIO_FLAGS(LED0_NODE, gpios)
// #else
// /* A build error here means your board isn't set up to blink an LED. */
// #error "Unsupported board: led0 devicetree alias is not defined"
// #define LED0    ""
// #define PIN     0
// #define FLAGS   0
// #endif

// #define SW0_NODE	DT_ALIAS(sw0)
// #if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
// #error "Unsupported board: sw0 devicetree alias is not defined"
// #endif

#define SERVER_ADDR "fd00:64::192.168.2.159"
#define SERVER_PORT 1883
#define MQTT_CLIENTID "zephyr_publisher"
#define APP_CONNECT_TIMEOUT_MS 2000
#define APP_SLEEP_MSECS 500
#define APP_CONNECT_TRIES 10
#define APP_MQTT_BUFFER_SIZE 128


// static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
// 							      {0});
// static struct gpio_callback button_cb_data;


// static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
// 						     {0});


static uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

static struct mqtt_client client_ctx;
static struct sockaddr_storage broker;
static struct zsock_pollfd fds[1];
static int nfds;
static bool connected;


static bool switch_state;

// static bool led_is_on = true;
// void button_pressed(const struct device *dev, struct gpio_callback *cb,
// 		    uint32_t pins)
// {
// 	// printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
// 	// gpio_pin_set_dt(&led, (int)led_is_on);
// 	led_is_on = !led_is_on;
// }


static void prepare_fds(struct mqtt_client *client)
{
	fds[0].fd = client->transport.tcp.sock;
	fds[0].events = ZSOCK_POLLIN;
	nfds = 1;
}

static void clear_fds(void)
{
	nfds = 0;
}

static int wait(int timeout)
{
	int ret = 0;

	if (nfds > 0) {
		ret = zsock_poll(fds, nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

void mqtt_evt_handler(struct mqtt_client *const client,
		      const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		connected = true;
		LOG_INF("MQTT client connected!");

		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);

		connected = false;
		clear_fds();

		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);

		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}

		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id
		};

		err = mqtt_publish_qos2_release(client, &rel_param);
		if (err != 0) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}

		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u",
			evt->param.pubcomp.message_id);

		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	default:
		break;
	}
}

static char *get_mqtt_payload(enum mqtt_qos qos)
{
	static char payload[4];

	if (switch_state) {
		strcpy(payload, "ON");
	}
	else {
		strcpy(payload, "OFF");
	}

	switch_state = !switch_state;

	return payload;
}

static char *get_mqtt_topic(void)
{
	return "room/julie/switch/light/state";
}

static int publish(struct mqtt_client *client, enum mqtt_qos qos)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = (uint8_t *)get_mqtt_topic();
	param.message.topic.topic.size =
			strlen(param.message.topic.topic.utf8);
	param.message.payload.data = get_mqtt_payload(qos);
	param.message.payload.len =
			strlen(param.message.payload.data);
	param.message_id = sys_rand32_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;

	return mqtt_publish(client, &param);
}

#define RC_STR(rc) ((rc) == 0 ? "OK" : "ERROR")

#define PRINT_RESULT(func, rc) \
	LOG_INF("%s: %d <%s>", (func), rc, RC_STR(rc))

static void broker_init(void)
{
	struct sockaddr_in6 *broker6 = (struct sockaddr_in6 *)&broker;

	broker6->sin6_family = AF_INET6;
	broker6->sin6_port = htons(SERVER_PORT);
	zsock_inet_pton(AF_INET6, SERVER_ADDR, &broker6->sin6_addr);
}

static void client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);

	broker_init();

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
	client->client_id.size = strlen(MQTT_CLIENTID);
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;

}

static int try_to_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	while (i++ < APP_CONNECT_TRIES && !connected) {

		client_init(client);

		rc = mqtt_connect(client);
		if (rc != 0) {
			PRINT_RESULT("mqtt_connect", rc);
			k_sleep(K_MSEC(APP_SLEEP_MSECS));
			continue;
		}

		prepare_fds(client);

		if (wait(APP_CONNECT_TIMEOUT_MS)) {
			mqtt_input(client);
		}

		if (!connected) {
			mqtt_abort(client);
		}
	}

	if (connected) {
		return 0;
	}

	return -EINVAL;
}

static int process_mqtt_and_sleep(struct mqtt_client *client, int timeout)
{
	int64_t remaining = timeout;
	int64_t start_time = k_uptime_get();
	int rc;

	while (remaining > 0 && connected) {
		if (wait(remaining)) {
			rc = mqtt_input(client);
			if (rc != 0) {
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		rc = mqtt_live(client);
		if (rc != 0 && rc != -EAGAIN) {
			PRINT_RESULT("mqtt_live", rc);
			return rc;
		} else if (rc == 0) {
			rc = mqtt_input(client);
			if (rc != 0) {
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		remaining = timeout + start_time - k_uptime_get();
	}

	return 0;
}

#define SUCCESS_OR_EXIT(rc) { if (rc != 0) { return 1; } }
#define SUCCESS_OR_BREAK(rc) { if (rc != 0) { break; } }

static int publisher(void)
{
	int i, rc, r = 0;

	LOG_INF("attempting to connect: ");
	rc = try_to_connect(&client_ctx);
	PRINT_RESULT("try_to_connect", rc);
	if (rc != 0)
		return -1;

	i = 0;

	rc = mqtt_ping(&client_ctx);
	PRINT_RESULT("mqtt_ping", rc);
	if (rc != 0)
		goto err;

	rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
	if (rc != 0)
		goto err;

	rc = publish(&client_ctx, MQTT_QOS_0_AT_MOST_ONCE);
	PRINT_RESULT("mqtt_publish", rc);
	if (rc != 0)
		goto err;

	rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
	if (rc != 0)
		goto err;

	// rc = publish(&client_ctx, MQTT_QOS_1_AT_LEAST_ONCE);
	// PRINT_RESULT("mqtt_publish", rc);
	// SUCCESS_OR_BREAK(rc);

	// rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
	// SUCCESS_OR_BREAK(rc);

	// rc = publish(&client_ctx, MQTT_QOS_2_EXACTLY_ONCE);
	// PRINT_RESULT("mqtt_publish", rc);
	// SUCCESS_OR_BREAK(rc);

	// rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
	// SUCCESS_OR_BREAK(rc);

err:
	rc = mqtt_disconnect(&client_ctx);
	PRINT_RESULT("mqtt_disconnect", rc);

	LOG_INF("Bye!");

	return r;
}

void main(void)
{
	const struct device *cons = device_get_binding(CONSOLE_LABEL);

	// int ret;
	// const struct device *cons = device_get_binding(CONSOLE_LABEL);

	// if (!device_is_ready(button.port)) {
	// 	printk("Error: button device %s is not ready\n",
	// 	       button.port->name);
	// 	return;
	// }

	// ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	// if (ret != 0) {
	// 	printk("Error %d: failed to configure %s pin %d\n",
	// 	       ret, button.port->name, button.pin);
	// 	return;
	// }

	// ret = gpio_pin_interrupt_configure_dt(&button,
	// 				      GPIO_INT_LEVEL_LOW);
	// if (ret != 0) {
	// 	printk("Error %d: failed to configure interrupt on %s pin %d\n",
	// 		ret, button.port->name, button.pin);
	// 	return;
	// }

	// gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	// gpio_add_callback(button.port, &button_cb_data);
	// printk("Set up button at %s pin %d\n", button.port->name, button.pin);

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


	// for (ret = 0; ret < 5; ret++) {
	// 	led_is_on = !led_is_on;
	// 	k_sleep(K_SECONDS(1));
	// }

	// k_sleep(K_SECONDS(5));

	if (event_manager_init()) {
		LOG_ERR("Event manager not initialized");
	} else {
		module_set_state(MODULE_STATE_READY);
	}


	// publisher();


	LOG_INF("****************************************");
	LOG_INF("MAIN DONE");
	LOG_INF("****************************************");



	k_sleep(K_SECONDS(3));
	pm_device_state_set(cons, PM_DEVICE_STATE_SUSPENDED);

	LOG_INF("PM_DEVICE_ACTION_SUSPEND");
}

static bool event_handler(const struct event_header *eh)
{
	// if (is_button_event(eh)) {
	// 	return handle_button_event(cast_button_event(eh));
	// }

	// if (is_module_state_event(eh)) {
	// 	const struct module_state_event *event = cast_module_state_event(eh);

	// 	if (check_state(event, MODULE_ID(leds), MODULE_STATE_READY)) {
	// 		/* Turn on the first LED */
	// 		send_led_event(LED_ID_0, &led_effect_on);
	// 	}

	// 	return false;
	// }

	// /* Event not handled but subscribed. */
	// __ASSERT_NO_MSG(false);

	// LOG_PRINTK("event_handler");

	// publisher();

	const struct button_event *evt;

	if (is_button_event(eh)) {
		evt = cast_button_event(eh);

		if (evt->pressed) {
			publisher();
		}
	}

	return true;
}

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, button_event);
