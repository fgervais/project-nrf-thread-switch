#include <stdio.h>
#include <openthread/netdata.h>
#include <openthread/thread.h>
#include <zephyr/net/openthread.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(openthread, LOG_LEVEL_DBG);

#define CSL_LOW_LATENCY_PERIOD_MS 	10
#define CSL_NORMAL_LATENCY_PERIOD_MS 	500

#define OPENTHREAD_READY_EVENT		BIT(0)


static K_EVENT_DEFINE(events);


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

void openthread_set_csl_period_ms(int period_ms)
{
	otError otErr;
	otInstance *instance = openthread_get_default_instance();

	otErr = otLinkCslSetPeriod(instance,
			period_ms * 1000 / OT_US_PER_TEN_SYMBOLS);
}

bool openthread_is_ready()
{
	return k_event_wait(&events, OPENTHREAD_READY_EVENT, false, K_NO_WAIT);
}

void openthread_set_low_latency()
{
	otLinkSetPollPeriod(openthread_get_default_instance(), 10);
	// openthread_set_csl_period_ms(CSL_LOW_LATENCY_PERIOD_MS);
}

void openthread_set_normal_latency()
{
	otLinkSetPollPeriod(openthread_get_default_instance(), 0);
	// openthread_set_csl_period_ms(CSL_NORMAL_LATENCY_PERIOD_MS);
}

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

	return openthread_start(ot_context);
}

int openthread_wait_for_ready(void)
{
	k_event_wait(&events, OPENTHREAD_READY_EVENT, false, K_FOREVER);

	return 0;
}
