#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <app_event_manager.h>
#include <openthread/thread.h>
#include <pm/device.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/openthread.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <net/socket.h>
#include <net/mqtt.h>
#include <random/rand32.h>

#define MODULE main
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>

#define SERVER_PORT 1883
#define MQTT_CLIENTID "zephyr_publisher"
#define APP_CONNECT_TIMEOUT_MS 2000
#define APP_SLEEP_MSECS 50
#define APP_CONNECT_TRIES 10
#define APP_MQTT_BUFFER_SIZE 128

// #define MQTT_TOPIC "home/room/julie/switch/light/state"
#define MQTT_TOPIC "home/room/computer/switch/table_light/state"

#define DNS_TIMEOUT (2 * MSEC_PER_SEC)


static uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

char *mqtt_server_addr = NULL;
bool dns_resolve_finished = false;

static struct mqtt_client client_ctx;
static struct sockaddr_storage broker;
static struct zsock_pollfd fds[1];
static int nfds;
static bool connected;

static bool switch_state;


void dns_result_cb(enum dns_resolve_status status,
		   struct dns_addrinfo *info,
		   void *user_data)
{
	char hr_addr[NET_IPV6_ADDR_LEN];
	char *hr_family;

	otInstance *instance = openthread_get_default_instance();


	k_sleep(K_MSEC(50));
	otLinkSetPollPeriod(instance, 0);

	switch (status) {
	case DNS_EAI_CANCELED:
		LOG_INF("DNS query was canceled");
		goto out;
	case DNS_EAI_FAIL:
		LOG_INF("DNS resolve failed");
		goto out;
	case DNS_EAI_NODATA:
		LOG_INF("Cannot resolve address");
		goto out;
	case DNS_EAI_ALLDONE:
		LOG_INF("DNS resolving finished");
		goto out;
	case DNS_EAI_INPROGRESS:
		break;
	default:
		LOG_INF("DNS resolving error (%d)", status);
		goto out;
	}

	if (!info) {
		goto out;
	}

	if (info->ai_family == AF_INET) {
		// hr_family = "IPv4";
		// mqtt_server_addr = &net_sin(&info->ai_addr)->sin_addr;
		LOG_ERR("We need an ipv6 address but received ipv4");
	} else if (info->ai_family == AF_INET6) {
		hr_family = "IPv6";
		mqtt_server_addr = (char *)&net_sin6(&info->ai_addr)->sin6_addr;
	} else {
		LOG_ERR("Invalid IP address family %d", info->ai_family);
		goto out;
	}

	LOG_INF("%s %s address: %s", user_data ? (char *)user_data : "<null>",
		hr_family,
		net_addr_ntop(info->ai_family, mqtt_server_addr,
					 hr_addr, sizeof(hr_addr)));
out:
	dns_resolve_finished = true;
}

static void do_ipv6_lookup(void)
{
	static const char *query = "home.home.arpa";
	static uint16_t dns_id;
	int ret;

	otInstance *instance = openthread_get_default_instance();


	otLinkSetPollPeriod(instance, 10);

	ret = dns_get_addr_info(query,
				DNS_QUERY_TYPE_AAAA,
				&dns_id,
				dns_result_cb,
				(void *)query,
				DNS_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Cannot resolve IPv6 address (%d)", ret);
		return;
	}

	LOG_DBG("DNS id %u", dns_id);
}


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

	otInstance *instance = openthread_get_default_instance();

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

	k_sleep(K_MSEC(50));
	otLinkSetPollPeriod(instance, 0);
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
	return MQTT_TOPIC;
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
	zsock_inet_pton(AF_INET6, mqtt_server_addr, &broker6->sin6_addr);
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
	int rc, r = 0;

	otInstance *instance = openthread_get_default_instance();

	LOG_INF("attempting to connect: ");
	otLinkSetPollPeriod(instance, 10);

	rc = try_to_connect(&client_ctx);
	PRINT_RESULT("try_to_connect", rc);
	if (rc != 0)
		return -1;

	rc = publish(&client_ctx, MQTT_QOS_0_AT_MOST_ONCE);
	PRINT_RESULT("mqtt_publish", rc);
	if (rc != 0)
		goto err;

	rc = process_mqtt_and_sleep(&client_ctx, 0);
	if (rc != 0)
		goto err;

err:
	rc = mqtt_disconnect(&client_ctx);
	PRINT_RESULT("mqtt_disconnect", rc);

	LOG_INF("Bye!");

	return r;
}

void main(void)
{
	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (app_event_manager_init()) {
		LOG_ERR("Event manager not initialized");
	} else {
		module_set_state(MODULE_STATE_READY);
	}

	// Wait a bit for Thread to initialize
	k_sleep(K_MSEC(100));

	while (1) {
		dns_resolve_finished = false;
		do_ipv6_lookup();

		while (!dns_resolve_finished)
			k_sleep(K_MSEC(100));

		if (mqtt_server_addr != NULL)
			break;
	}

	LOG_INF("****************************************");
	LOG_INF("MAIN DONE");
	LOG_INF("****************************************");

	k_sleep(K_SECONDS(3));
	// pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

	LOG_INF("PM_DEVICE_ACTION_SUSPEND");
}

static bool event_handler(const struct app_event_header *eh)
{
	const struct button_event *evt;

	if (is_button_event(eh)) {
		evt = cast_button_event(eh);

		if (evt->pressed) {
			publisher();
		}
	}

	return true;
}

APP_EVENT_LISTENER(MODULE, event_handler);
APP_EVENT_SUBSCRIBE(MODULE, button_event);
