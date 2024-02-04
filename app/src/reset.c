#include <zephyr/drivers/hwinfo.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(reset, LOG_LEVEL_DBG);


static const char *cause_to_string(uint32_t cause)
{
	switch (cause) {
	case RESET_PIN:
		return "pin";

	case RESET_SOFTWARE:
		return "software";

	case RESET_BROWNOUT:
		return "brownout";

	case RESET_POR:
		return "power-on reset";

	case RESET_WATCHDOG:
		return "watchdog";

	case RESET_DEBUG:
		return "debug";

	case RESET_SECURITY:
		return "security";

	case RESET_LOW_POWER_WAKE:
		return "low power wake-up";

	case RESET_CPU_LOCKUP:
		return "CPU lockup";

	case RESET_PARITY:
		return "parity error";

	case RESET_PLL:
		return "PLL error";

	case RESET_CLOCK:
		return "clock";

	case RESET_HARDWARE:
		return "hardware";

	case RESET_USER:
		return "user";

	case RESET_TEMPERATURE:
		return "temperature";

	default:
		return "unknown";
	}
}

static void print_all_reset_causes(uint32_t cause)
{
	for (uint32_t cause_mask = 1; cause_mask; cause_mask <<= 1) {
		if (cause & cause_mask) {
			LOG_INF("✨ reset cause: %s",
				    cause_to_string(cause & cause_mask));
		}
	}
}

int show_reset_cause(void)
{
	int ret;
	uint32_t cause;

	ret = hwinfo_get_reset_cause(&cause);
	if (ret == -ENOTSUP) {
		LOG_ERR("reset cause: not supported by hardware");
		return ret;
	} else if (ret != 0) {
		LOG_ERR("reset cause: error reading the cause [%d]", ret);
		return ret;
	}

	if (cause != 0) {
		print_all_reset_causes(cause);
	} else {
		LOG_INF("no reset cause set");
	}

	return cause;
}

int clear_reset_cause(void)
{
	int ret;

	ret = hwinfo_clear_reset_cause();
	if (ret == -ENOTSUP) {
		LOG_ERR("Reset cause clear not supported by hardware");
	} else if (ret != 0) {
		LOG_ERR("Error clearing the reset causes [%d]", ret);
		return ret;
	}

	return 0;
}

bool is_reset_cause_watchdog(uint32_t cause)
{
	return cause & RESET_WATCHDOG;
}

bool is_reset_cause_button(uint32_t cause)
{
	return cause & RESET_PIN;
}

bool is_reset_cause_software(uint32_t cause)
{
	return cause & RESET_SOFTWARE;
}
