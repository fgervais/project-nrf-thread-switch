#include <stdio.h>
#include <openthread/child_supervision.h>
#include <openthread/netdata.h>
#include <openthread/thread.h>
#include <zephyr/net/openthread.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(openthread, LOG_LEVEL_DBG);

// #define CSL_LOW_LATENCY_PERIOD_MS 	10
#define CSL_NORMAL_LATENCY_PERIOD_MS 	500

#define LOW_LATENCY_POLL_PERIOD_MS	10

#define OPENTHREAD_READY_EVENT		BIT(0)

#define LOW_LATENCY_EVENT_REQ_LOW	BIT(0)
#define LOW_LATENCY_EVENT_REQ_NORMAL	BIT(1)
#define LOW_LATENCY_EVENT_FORCE_NORMAL	BIT(2)

static K_EVENT_DEFINE(events);
static K_EVENT_DEFINE(low_latency_events);


static void format_address(char *buffer, size_t buffer_size, const uint8_t *addr_m8,
			   size_t addr_size)
{
	if (addr_size == 0) {
		buffer[0] = 0;
		return;
	}

	size_t pos = 0;

	for (size_t i = 0; i < addr_size; i++) {
		int ret = snprintf(&buffer[pos], buffer_size - pos, "%.2x", addr_m8[i]);

		if ((ret > 0) && ((size_t)ret < buffer_size - pos)) {
			pos += ret;
		} else {
			break;
		}
	}
}

static bool check_neighbors(otInstance *instance)
{
        otNeighborInfoIterator iterator = OT_NEIGHBOR_INFO_ITERATOR_INIT;
        otNeighborInfo info;

        bool has_neighbors = false;

        while (otThreadGetNextNeighborInfo(instance, &iterator, &info) == OT_ERROR_NONE) {
                char addr_str[ARRAY_SIZE(info.mExtAddress.m8) * 2 + 1];

                format_address(addr_str, sizeof(addr_str), info.mExtAddress.m8,
                               ARRAY_SIZE(info.mExtAddress.m8));
                LOG_INF("Neighbor addr:%s age:%" PRIu32, addr_str, info.mAge);

                has_neighbors = true;

                if (!IS_ENABLED(CONFIG_LOG)) {
                        /* If logging is disabled, stop when a neighbor is found. */
                        break;
                }
        }

        return has_neighbors;
}

static bool check_routes(otInstance *instance)
{
        otNetworkDataIterator iterator = OT_NETWORK_DATA_ITERATOR_INIT;
        otBorderRouterConfig config;

        bool route_available = false;

        while (otNetDataGetNextOnMeshPrefix(instance, &iterator, &config) == OT_ERROR_NONE) {
                char addr_str[ARRAY_SIZE(config.mPrefix.mPrefix.mFields.m8) * 2 + 1] = {0};

                format_address(addr_str, sizeof(addr_str), config.mPrefix.mPrefix.mFields.m8,
                               ARRAY_SIZE(config.mPrefix.mPrefix.mFields.m8));
                LOG_INF("Route prefix:%s default:%s preferred:%s", addr_str,
                        (config.mDefaultRoute)?("yes"):("no"),
                        (config.mPreferred)?("yes"):("no"));

                // route_available = route_available || config.mDefaultRoute;
                route_available = true;

                if (route_available && !IS_ENABLED(CONFIG_LOG)) {
                        /* If logging is disabled, stop when a route is found. */
                        break;
                }
        }

        return route_available;
}

// nrf/subsys/caf/modules/net_state_ot.c
static void on_thread_state_changed(otChangedFlags flags,
				    struct openthread_context *ot_context,
				    void *user_data)
{
	static bool has_role;

	bool has_neighbors = check_neighbors(ot_context->instance);
	bool route_available = check_routes(ot_context->instance);
	bool has_address = flags & OT_CHANGED_IP6_ADDRESS_ADDED;

	LOG_INF("state: 0x%.8x has_neighbours:%s route_available:%s", flags,
		(has_neighbors)?("yes"):("no"), (route_available)?("yes"):("no"));

	if (flags & OT_CHANGED_THREAD_ROLE) {
		switch (otThreadGetDeviceRole(ot_context->instance)) {
		case OT_DEVICE_ROLE_LEADER:
			LOG_INF("Leader role set");
			has_role = true;
			break;

		case OT_DEVICE_ROLE_CHILD:
			LOG_INF("Child role set");
			has_role = true;
			break;

		case OT_DEVICE_ROLE_ROUTER:
			LOG_INF("Router role set");
			has_role = true;
			break;

		case OT_DEVICE_ROLE_DISABLED:
		case OT_DEVICE_ROLE_DETACHED:
		default:
			LOG_INF("No role set");
			has_role = false;
			break;
		}
	}

	if (has_role && has_neighbors && route_available && has_address) {
		LOG_INF("🛜  openthread ready!");
		k_event_post(&events, OPENTHREAD_READY_EVENT);
	} else {
		k_event_set(&events, 0);
	}
}

static struct openthread_state_changed_cb ot_state_chaged_cb = {
	.state_changed_cb = on_thread_state_changed
};

static bool is_mtd_in_med_mode(otInstance *instance)
{
	return otThreadGetLinkMode(instance).mRxOnWhenIdle;
}

// static void openthread_set_csl_period_ms(int period_ms)
// {
// 	otError otErr;
// 	otInstance *instance = openthread_get_default_instance();

// 	otErr = otLinkCslSetPeriod(instance,
// 			period_ms * 1000 / OT_US_PER_TEN_SYMBOLS);
// }

bool openthread_is_ready()
{
	return k_event_wait(&events, OPENTHREAD_READY_EVENT, false, K_NO_WAIT);
}

static void openthread_set_low_latency()
{
	struct openthread_context *ot_context = openthread_get_default_context();

	if (is_mtd_in_med_mode(ot_context->instance)) {
		return;
	}

	LOG_INF("   └── ⏩ start low latency");

	openthread_api_mutex_lock(ot_context);
	otLinkSetPollPeriod(ot_context->instance, LOW_LATENCY_POLL_PERIOD_MS);
	openthread_api_mutex_unlock(ot_context);
	// openthread_set_csl_period_ms(CSL_LOW_LATENCY_PERIOD_MS);
}

static void openthread_set_normal_latency()
{
	struct openthread_context *ot_context = openthread_get_default_context();

	if (is_mtd_in_med_mode(ot_context->instance)) {
		return;
	}

	LOG_INF("   └── ⏹️  stop low latency");

	openthread_api_mutex_lock(ot_context);
	otLinkSetPollPeriod(ot_context->instance, 0);
	openthread_api_mutex_unlock(ot_context);
	// openthread_set_csl_period_ms(CSL_NORMAL_LATENCY_PERIOD_MS);
}

void openthread_request_low_latency(const char *reason)
{
	LOG_INF("👋 request low latency (%s)", reason);

	k_event_post(&low_latency_events, LOW_LATENCY_EVENT_REQ_LOW);
}

void openthread_request_normal_latency(const char *reason)
{
	LOG_INF("👋 request normal latency (%s)", reason);

	k_event_post(&low_latency_events, LOW_LATENCY_EVENT_REQ_NORMAL);
}

void openthread_force_normal_latency(const char *reason)
{
	LOG_INF("👋 force normal latency (%s)", reason);

	k_event_post(&low_latency_events, LOW_LATENCY_EVENT_FORCE_NORMAL);
}

static void receive_latency_management_thread_function(void)
{
	int low_latency_request_level = 0;
	bool timeout_enabled = false;
	uint32_t events;
 
	while (1) {
		events = k_event_wait(&low_latency_events,
			(LOW_LATENCY_EVENT_REQ_LOW |
			 LOW_LATENCY_EVENT_REQ_NORMAL |
			 LOW_LATENCY_EVENT_FORCE_NORMAL),
			false,
			timeout_enabled ? K_SECONDS(3) : K_FOREVER);
		k_event_set(&low_latency_events, 0);

		LOG_INF("⏰ events: %08x", events);

		if (events == 0) {
			LOG_INF("   ├── ⚠️  low latency timeout");
		}

		if (events & LOW_LATENCY_EVENT_REQ_LOW) {
			openthread_set_low_latency();
			low_latency_request_level++;
			timeout_enabled = true;
		}
		else if (events & LOW_LATENCY_EVENT_REQ_NORMAL) {
			// We are already in normal latency and someone requested
			// normal latency.
			if (low_latency_request_level == 0) {
				LOG_INF("   └── latency already normal");
				continue;
			}

			low_latency_request_level--;

			if (low_latency_request_level == 0) {
				openthread_set_normal_latency();
				timeout_enabled = false;
			}
			else {
				LOG_INF("   └── low latency request level > 0");
			}
		}
		// Timeout or force low latency
		else if (events == 0 || events & LOW_LATENCY_EVENT_FORCE_NORMAL) {
			low_latency_request_level = 0;
			openthread_set_normal_latency();
			timeout_enabled = false;
		}
	}
}

K_THREAD_DEFINE(receive_latency_thread, CONFIG_APP_OT_LATENCY_THREAD_STACK_SIZE,
		receive_latency_management_thread_function, NULL, NULL, NULL,
		-2, 0, SYS_FOREVER_MS);

int openthread_erase_persistent_info(void)
{
	struct openthread_context *ot_context = openthread_get_default_context();
	otError err;

	openthread_api_mutex_lock(ot_context);
	err = otInstanceErasePersistentInfo(ot_context->instance);
	openthread_api_mutex_unlock(ot_context);

	if (err != OT_ERROR_NONE) {
		return -1;
	}

	return 0;
}

int openthread_my_start(void)
{
	int ret;
	struct openthread_context *ot_context = openthread_get_default_context();

	ret = openthread_state_changed_cb_register(ot_context,
						   &ot_state_chaged_cb);
	if (ret < 0) {
		LOG_ERR("Could register callback");
		return ret;
	}

	k_thread_start(receive_latency_thread);

	openthread_api_mutex_lock(ot_context);
	otLinkSetPollPeriod(ot_context->instance, CONFIG_OPENTHREAD_POLL_PERIOD);
	// Disable child supervision.
	// If enabled, there will be a child-parent communication every 190s. 
	otChildSupervisionSetCheckTimeout(ot_context->instance, 0);
	otChildSupervisionSetInterval(ot_context->instance, 0);
	otThreadSetChildTimeout(
		ot_context->instance,
		(int)(CONFIG_OPENTHREAD_POLL_PERIOD / 1000) + 4);
	openthread_api_mutex_unlock(ot_context);

	return openthread_start(ot_context);
}

int openthread_wait_for_ready(void)
{
	k_event_wait(&events, OPENTHREAD_READY_EVENT, false, K_FOREVER);

	return 0;
}
