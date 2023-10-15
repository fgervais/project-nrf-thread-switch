#include <zephyr/drivers/watchdog.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/rand32.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mqtt, LOG_LEVEL_DBG);

#include "mqtt.h"
#include "openthread.h"


#define MQTT_EVENT_CONNECTED		BIT(0)


static uint8_t rx_buffer[CONFIG_APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[CONFIG_APP_MQTT_BUFFER_SIZE];

static struct mqtt_client client_ctx;

static struct mqtt_topic last_will_topic;
static struct mqtt_utf8 last_will_message;

static struct sockaddr_storage broker;

/* Socket Poll */
static struct zsock_pollfd fds[1];
static int nfds;

static const char *device_id;

static const struct mqtt_subscription *subscriptions;
static size_t number_of_subscriptions;

static struct k_work_delayable keepalive_work;

#if defined(CONFIG_DNS_RESOLVER)
static struct zsock_addrinfo hints;
static struct zsock_addrinfo *haddr;
#endif

static const struct device *wdt;
static int wdt_channel_id;

static K_EVENT_DEFINE(mqtt_events);


static int connect_to_server(void);
static void keepalive(struct k_work *work);
static void mqtt_event_handler(struct mqtt_client *const client,
			       const struct mqtt_evt *evt);


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

static int poll_socket(int timeout)
{
	int rc = -EINVAL;

	if (nfds <= 0) {
		return rc;
	}

	// Peer disconnect counts as a read event.
	// https://stackoverflow.com/questions/17692447/does-poll-system-call-know-if-remote-socket-closed-or-disconnected#:~:text=Peer%20disconnect%20counts%20as%20a%20read%20event.
	rc = zsock_poll(fds, nfds, timeout);
	if (rc < 0) {
		LOG_ERR("poll error: %d", errno);
		return -errno;
	}

	return rc;
}

static void broker_init(void)
{
	struct sockaddr_in6 *broker6 = (struct sockaddr_in6 *)&broker;

	broker6->sin6_family = AF_INET6;
	broker6->sin6_port = htons(CONFIG_APP_MQTT_SERVER_PORT);

#if defined(CONFIG_DNS_RESOLVER)
	net_ipaddr_copy(&broker6->sin6_addr,
			&net_sin6(haddr->ai_addr)->sin6_addr);
#else
	zsock_inet_pton(AF_INET6, CONFIG_APP_MQTT_SERVER_ADDR, &broker6->sin_addr);
#endif

	k_work_init_delayable(&keepalive_work, keepalive);
}

static void client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);

	broker_init();

	client->broker = &broker;
	client->evt_cb = mqtt_event_handler;

	// The MQTT server will remember our subscriptions based on this ID
	// if ever we get disconnected.
	// 
	// https://stackoverflow.com/questions/75927301/why-should-the-clients-of-mqtt-choose-their-own-id
	// https://docs.zephyrproject.org/latest/connectivity/networking/api/mqtt.html#c.mqtt_client.clean_session
	client->client_id.utf8 = (uint8_t *)device_id;
	client->client_id.size = strlen(device_id);

	LOG_DBG("client id: %s", device_id);

	client->password = NULL;
	client->user_name = NULL;

	client->protocol_version = MQTT_VERSION_3_1_1;

	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	client->transport.type = MQTT_TRANSPORT_NON_SECURE;

	client->will_topic = &last_will_topic;
	client->will_message = &last_will_message;
	client->will_retain = 1;
}

static bool is_mqtt_connected(void)
{
	// return k_event_test(&mqtt_events, MQTT_EVENT_CONNECTED);
	return k_event_wait(&mqtt_events, MQTT_EVENT_CONNECTED, false, K_NO_WAIT);
}

static void mqtt_connected(void)
{
	k_event_set(&mqtt_events, MQTT_EVENT_CONNECTED);
}

static void mqtt_disconnected(void)
{
	k_event_clear(&mqtt_events, MQTT_EVENT_CONNECTED);
	clear_fds();
}

static void wait_for_mqtt_connected(void)
{
	k_event_wait(&mqtt_events, MQTT_EVENT_CONNECTED, false, K_FOREVER);
}

static void mqtt_event_handler(struct mqtt_client *const client,
		      const struct mqtt_evt *evt)
{
	uint8_t data[33];
	int len;
	int bytes_read;

	switch (evt->type) {
	case MQTT_EVT_SUBACK:
		openthread_request_normal_latency("MQTT_EVT_SUBACK");

		LOG_INF("SUBACK packet id: %u", evt->param.suback.message_id);
		break;

	case MQTT_EVT_UNSUBACK:
		openthread_request_normal_latency("MQTT_EVT_UNSUBACK");

		LOG_INF("UNSUBACK packet id: %u", evt->param.suback.message_id);
		break;

	case MQTT_EVT_CONNACK:
		openthread_request_normal_latency("MQTT_EVT_CONNACK");

		if (evt->result) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		LOG_INF("MQTT client connected!");
		mqtt_connected();
		break;

	case MQTT_EVT_DISCONNECT:
		openthread_force_normal_latency("MQTT_EVT_DISCONNECT");

		LOG_INF("MQTT client disconnected %d", evt->result);

		// We let keepalive running so on next ping it will reconnect
		// so we will be back online and the watchdog will get fed.
		mqtt_disconnected();
		break;

	case MQTT_EVT_PUBACK:
		openthread_request_normal_latency("MQTT_EVT_PUBACK");

		if (evt->result) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_PUBLISH:
		len = evt->param.publish.message.payload.len;

		LOG_INF("📨 MQTT publish received %d, %d bytes", evt->result, len);
		LOG_INF("   ├── id: %d, qos: %d", evt->param.publish.message_id,
			evt->param.publish.message.topic.qos);
		LOG_INF("   ├── topic: %.*s",
			evt->param.publish.message.topic.topic.size,
			evt->param.publish.message.topic.topic.utf8);

		while (len) {
			bytes_read = mqtt_read_publish_payload(&client_ctx,
					data,
					len >= sizeof(data) - 1 ?
					sizeof(data) - 1 : len);
			if (bytes_read < 0 && bytes_read != -EAGAIN) {
				LOG_ERR("failure to read payload");
				break;
			}

			data[bytes_read] = '\0';
			LOG_INF("   └── payload: %s", data);
			len -= bytes_read;
		}

		for (int i = 0; i < number_of_subscriptions; i++) {
			if (strncmp(subscriptions[i].topic,
				    evt->param.publish.message.topic.topic.utf8,
				    evt->param.publish.message.topic.topic.size) == 0) {
				subscriptions[i].callback(data);
				break;
			}
		}
		break;

	case MQTT_EVT_PINGRESP:
		openthread_request_normal_latency("MQTT_EVT_PINGRESP");

		LOG_INF("PINGRESP");
		LOG_INF("└── 🦴 feed watchdog");
		wdt_feed(wdt, wdt_channel_id);
		break;

	default:
		LOG_WRN("Unhandled MQTT event %d", evt->type);
		break;
	}
}

static int subscribe(struct mqtt_client *client, const char *topic)
{
	int ret;
	struct mqtt_topic subs_topic;
	struct mqtt_subscription_list subs_list;

	subs_topic.topic.utf8 = topic;
	subs_topic.topic.size = strlen(topic);
	subs_topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	subs_list.list = &subs_topic;
	subs_list.list_count = 1U;
	subs_list.message_id = 1U;

	openthread_request_low_latency("mqtt_subscribe");

	ret = mqtt_subscribe(client, &subs_list);
	if (ret) {
		openthread_request_normal_latency("mqtt_subscribe error");
		LOG_ERR("Failed on topic %s", topic);
		return ret;
	}

	return 0;
}

static void keepalive(struct k_work *work)
{
	int ret;

	LOG_INF("🤖 mqtt keepalive");

	if (!is_mqtt_connected()) {
		ret = connect_to_server();
		if (ret < 0) {
			LOG_ERR("Failed to connect to server");
			goto out;
		}
	}

	if (client_ctx.unacked_ping) {
		LOG_WRN("🤔 MQTT ping not acknowledged: %d",
			client_ctx.unacked_ping);
	}

	openthread_request_low_latency("mqtt_ping");

	ret = mqtt_ping(&client_ctx);
	if (ret < 0) {
		openthread_request_normal_latency("mqtt_ping error");
		LOG_ERR("mqtt_ping (%d)", ret);
	}

out:
	k_work_reschedule(&keepalive_work, K_SECONDS(client_ctx.keepalive));
}

static void mqtt_receive_thread_function(void)
{
	int rc;

	while (1) {
		wait_for_mqtt_connected();
		if (poll_socket(SYS_FOREVER_MS)) {
			LOG_INF("mqtt_receive_thread: mqtt_input()");
			rc = mqtt_input(&client_ctx);
			if (rc < 0) {
				LOG_WRN("⚠️  mqtt_input (%d)", rc);
			}
		}
	}
}

K_THREAD_DEFINE(mqtt_receive_thread, CONFIG_APP_MQTT_RECEIVE_THREAD_STACK_SIZE,
		mqtt_receive_thread_function, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, SYS_FOREVER_MS);


#define MQTT_CONNECT_TIMEOUT_MS	1000
#define MQTT_ABORT_TIMEOUT_MS	5000
static int try_to_connect(struct mqtt_client *client)
{
	uint8_t retries = 3U;
	int rc;

	LOG_INF("attempting to connect");

	while (retries--) {
		client_init(client);

		openthread_request_low_latency("mqtt_connect");

		rc = mqtt_connect(client);
		if (rc) {
			openthread_request_normal_latency("mqtt_connect error");
			LOG_ERR("mqtt_connect failed %d", rc);
			continue;
		}

		// This is after `mqtt_connect()` so can get the socket descriptor
		prepare_fds(client);

		rc = poll_socket(MQTT_CONNECT_TIMEOUT_MS);
		if (rc < 0) {
			openthread_request_normal_latency("mqtt_connect error");
			goto abort;
		}

		mqtt_input(client);

		if (is_mqtt_connected()) {
			// If we are reconnecting and so a keepalive work is
			// already pending, this function will replace the
			// currently pending one.
			// 
			// https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html#scheduling-a-delayable-work-item
			k_work_reschedule(&keepalive_work,
					  K_SECONDS(client->keepalive));
			// Does nothing if thread is already started.
			// https://github.com/zephyrproject-rtos/zephyr/blob/cdebe6ef711ffbd08b5faad48b955fdd077e00fc/kernel/sched.c#L658C37-L658C37
			k_thread_start(mqtt_receive_thread);
			return 0;
		}

abort:
		mqtt_abort(client);
		poll_socket(MQTT_ABORT_TIMEOUT_MS);
	}

	return -EINVAL;
}

#if defined(CONFIG_DNS_RESOLVER)
static int get_mqtt_broker_addrinfo(void)
{
	int retries = 3;
	int rc = -EINVAL;

	LOG_INF("resolving server address");

	while (retries--) {
		hints.ai_family = AF_INET6;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = 0;

		openthread_request_low_latency("getaddrinfo");

		// Return value:
		// zephyr/include/zephyr/net/dns_resolve.h
		// enum dns_resolve_status
		rc = getaddrinfo(CONFIG_APP_MQTT_SERVER_HOSTNAME,
				       STRINGIFY(CONFIG_APP_MQTT_SERVER_PORT),
				       &hints, &haddr);

		openthread_request_normal_latency("getaddrinfo done");

		if (rc == 0) {
			char atxt[INET6_ADDRSTRLEN] = { 0 };

			LOG_INF("DNS resolved for %s:%d",
			CONFIG_APP_MQTT_SERVER_HOSTNAME,
			CONFIG_APP_MQTT_SERVER_PORT);

			assert(haddr->ai_addr->sa_family == AF_INET6);

			inet_ntop(
			    AF_INET6,
			    &((const struct sockaddr_in6 *)haddr->ai_addr)->sin6_addr,
			    atxt, sizeof(atxt));

			LOG_INF("└── address: %s", atxt);

			return 0;
		}

		LOG_WRN("DNS not resolved for %s:%d (%d), retrying",
			CONFIG_APP_MQTT_SERVER_HOSTNAME,
			CONFIG_APP_MQTT_SERVER_PORT,
			rc);

		k_sleep(K_MSEC(200));
	}

	return rc;
}
#endif

static int connect_to_server(void)
{
	int rc = -EINVAL;

	LOG_INF("🔌 connect to server");

#if defined(CONFIG_DNS_RESOLVER)
	rc = get_mqtt_broker_addrinfo();
	if (rc) {
		return rc;
	}
#endif

	rc = try_to_connect(&client_ctx);
	if (rc) {
		return rc;
	}

	LOG_INF("mqtt keepalive: %ds", client_ctx.keepalive);

	return 0;
}

int mqtt_watchdog_init(const struct device *watchdog, int channel_id)
{
	wdt = watchdog;
	wdt_channel_id = channel_id;

	return 0;
}

int mqtt_publish_to_topic(const char *topic, char *payload, bool retain)
{
	int ret;
	struct mqtt_publish_param param;

	LOG_INF("📤 %s", topic);
	LOG_INF("   └── payload: %s", payload);

	// QOS 1 to receive an puback
	param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	param.message.topic.topic.utf8 = (uint8_t *)topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.payload.data = payload;
	param.message.payload.len = strlen(payload);
	param.message_id = sys_rand32_get();
	param.dup_flag = 0U;
	param.retain_flag = retain ? 1U : 0U;

	if (!is_mqtt_connected()) {
		ret = connect_to_server();
		if (ret < 0) {
			return ret;
		}
	}

	openthread_request_low_latency("mqtt_publish");

	ret = mqtt_publish(&client_ctx, &param);
	if (ret < 0) {
		openthread_request_normal_latency("mqtt_publish error");
		LOG_ERR("mqtt_publish (%d)", ret);
		return ret;
	}

	return 0;
}

int mqtt_subscribe_to_topic(const struct mqtt_subscription *subs,
			    size_t nb_of_subs)
{
	int ret;

	if (!is_mqtt_connected()) {
		ret = connect_to_server();
		if (ret < 0) {
			return ret;
		}
	}

	subscriptions = subs;
	number_of_subscriptions = nb_of_subs;

	for (int i = 0; i < nb_of_subs; i++) {
		LOG_INF("subscribing to topic: %s", subs[i].topic);
		ret = subscribe(&client_ctx, subs[i].topic);	
		if (ret < 0) {
			LOG_ERR("Could not subscribe to topic");
			return ret;
		}	
	}

	return 0;
}

int mqtt_init(const char *dev_id,
	      const char *last_will_topic_string,
	      const char *last_will_message_string)
{
	int ret;

	device_id = dev_id;

	last_will_topic.topic.utf8 = last_will_topic_string;
	last_will_topic.topic.size = strlen(last_will_topic_string);
	last_will_topic.qos = MQTT_QOS_0_AT_MOST_ONCE;

	last_will_message.utf8 = last_will_message_string;
	last_will_message.size = strlen(last_will_message_string);

	if (!is_mqtt_connected()) {
		ret = connect_to_server();
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}
