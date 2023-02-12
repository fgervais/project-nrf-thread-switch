#include <openthread/thread.h>
#include <zephyr/net/openthread.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(openthread, LOG_LEVEL_DBG);


bool openthread_ready = false;

// static void on_thread_state_changed(otChangedFlags flags,
// 				    struct openthread_context *ot_context,
// 				    void *user_data)
static void on_thread_state_changed(uint32_t flags, void *context)
{
	if (flags & OT_CHANGED_IP6_ADDRESS_ADDED) {
		LOG_INF("openthread ready!");
		openthread_ready = true;
	}
}

// static struct openthread_state_changed_cb ot_state_chaged_cb = {
// 	.state_changed_cb = on_thread_state_changed
// };

void openthread_enable_ready_flag()
{
	openthread_set_state_changed_cb(on_thread_state_changed);
	// openthread_state_changed_cb_register(openthread_get_default_context(),
	// 	&ot_state_chaged_cb);
}
