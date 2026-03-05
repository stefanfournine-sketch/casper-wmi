/**
 * casper-wmi.c - Casper Excalibur Laptop WMI Driver
 *
 * Copyright (C) 2024 Mustafa Ekşi <mustafa.eskieksi@gmail.com>
 *
 * This driver provides Linux kernel support for Casper Excalibur gaming laptops
 * via Windows Management Instrumentation (WMI). It exposes:
 *  - Keyboard backlight control (brightness and RGB color zones)
 *  - Hardware monitoring (CPU/GPU fan speeds)
 *  - Power plan switching via platform_profile
 *
 * The driver communicates with the laptop's ACPI firmware using a proprietary
 * WMI GUID. Supported models include G650, G750, G670, G870, and G900 series.
 *
 * Note: This is an out-of-tree module. The preferred solution is to use the
 * in-kernel driver available from Linux 6.x+.
 */

#include <linux/acpi.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/wmi.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/dmi.h>
#include <linux/platform_profile.h>
#include <acpi/acexcep.h>

MODULE_AUTHOR("Mustafa Ekşi <mustafa.eskieksi@gmail.com>");
MODULE_DESCRIPTION("Casper Excalibur Laptop WMI driver");
MODULE_LICENSE("GPL");


/** WMI GUID for all Casper Excalibur laptops */
#define CASPER_WMI_GUID "644C5791-B7B0-4123-A90B-E93876E0DAAD"

/** LED zone IDs for RGB control */
#define CASPER_KEYBOARD_LED_1 0x03	/* Left/main keyboard zone */
#define CASPER_KEYBOARD_LED_2 0x04	/* Right keyboard zone */
#define CASPER_KEYBOARD_LED_3 0x05	/* Top/function row zone */
#define CASPER_ALL_KEYBOARD_LEDS 0x06	/* All keyboard zones combined */
#define CASPER_CORNER_LEDS 0x07		/* Front edge/corner LEDs */

/** WMI command types */
#define CASPER_READ 0xfa00		/* Read/query operation */
#define CASPER_WRITE 0xfb00		/* Write/set operation */
#define CASPER_GET_HARDWAREINFO 0x0200	/* Get fan speeds */
#define CASPER_GET_BIOSVER 0x0201	/* Get BIOS version */
#define CASPER_SET_LED 0x0100		/* Set LED color/brightness */
#define CASPER_POWERPLAN 0x0300		/* Set power profile */


/* Compatibility macro for older kernels */
#ifndef to_wmi_device
#define to_wmi_device(device)	container_of(device, struct wmi_device, dev)
#endif

/**
 * struct casper_wmi_args - WMI argument structure for Casper commands
 * @a0: Direction (CASPER_READ or CASPER_WRITE)
 * @a1: Command type (CASPER_SET_LED, CASPER_GET_HARDWAREINFO, etc.)
 * @a2: Argument 2 (LED zone, power plan value, etc.)
 * @a3: Argument 3 (LED data low bits)
 * @a4: Argument 4 (CPU fan speed from hardware query)
 * @a5: Argument 5 (GPU fan speed from hardware query)
 * @a6: Argument 6 (reserved)
 * @rev0: Reserved field 0
 * @rev1: Reserved field 1
 *
 * This 32-byte structure is passed to the WMI firmware interface.
 */
struct casper_wmi_args {
	u16 a0, a1;
	u32 a2, a3, a4, a5, a6, rev0, rev1;
};


/* State tracking for LED color persistence across brightness changes */
static u32 last_keyboard_led_change;	/* Last color value written */
static u32 last_keyboard_led_zone;	/* Last zone written to */

/**
 * casper_raw_fanspeed - Pointer to fan speed format flag
 *
 * Points to has_raw_fanspeed or no_raw_fanspeed depending on DMI match.
 * Determines if fan speed values need byte swapping.
 */
static bool *casper_raw_fanspeed;


/**
 * dmi_matched() - Callback for DMI system identification
 * @dmi: DMI system ID structure
 *
 * Called when a matching laptop model is found. Sets the fan speed
 * format flag based on the driver_data field.
 *
 * Return: Always returns 1 to indicate match accepted
 */
static int dmi_matched(const struct dmi_system_id *dmi)
{
	pr_info("Identified laptop model '%s'\n", dmi->ident);
	casper_raw_fanspeed = dmi->driver_data;
	return 1;
}

/** Fan speed format flag - requires byte swapping on newer models */
static bool no_raw_fanspeed = false;

/**
 * casper_dmi_list - DMI table for supported laptop models
 *
 * Lists all supported Casper Excalibur models. Each entry specifies
 * the DMI strings to match and the appropriate fan speed format.
 */
static const struct dmi_system_id casper_dmi_list[] = {
	{
	 .callback = dmi_matched,
	 .ident = "CASPER EXCALIBUR G650",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "CASPER BILGISAYAR SISTEMLERI"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G650")
		     },
	 .driver_data = &no_raw_fanspeed,
        },
        {
	 .callback = dmi_matched,
	 .ident = "CASPER EXCALIBUR G750",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "CASPER BILGISAYAR SISTEMLERI"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G750")
		     },
	 .driver_data = &no_raw_fanspeed,
        },
        {
	 .callback = dmi_matched,
	 .ident = "CASPER EXCALIBUR G670",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "CASPER BILGISAYAR SISTEMLERI"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G670")
		     },
	 .driver_data = &no_raw_fanspeed,
        },
        {
	 .callback = dmi_matched,
	 .ident = "CASPER EXCALIBUR G870",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "CASPER BILGISAYAR SISTEMLERI"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G870")
		     },
	 .driver_data = &no_raw_fanspeed,
        },
        {
	 .callback = dmi_matched,
	 .ident = "CASPER EXCALIBUR G900",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "CASPER BILGISAYAR SISTEMLERI"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G900"),
                     DMI_MATCH(DMI_BIOS_VERSION, "CP131")
		     },
	 .driver_data = &no_raw_fanspeed,
	  },
	{ }
};


/**
 * casper_set() - Send a WMI write command to set LED or power plan
 * @a1: Command sub-type (CASPER_SET_LED or CASPER_POWERPLAN)
 * @zone_id: LED zone ID or power plan value
 * @data: LED color data (for CASPER_SET_LED)
 *
 * Sends a WMI write command to the Casper firmware. Used for:
 * - Setting LED colors (when a1=CASPER_SET_LED)
 * - Changing power plans (when a1=CASPER_POWERPLAN)
 *
 * Return: ACPI status code
 */
static acpi_status casper_set(u16 a1, u32 zone_id, u32 data)
{
	struct casper_wmi_args wmi_args = { 0 };
	wmi_args.a0 = CASPER_WRITE;
	wmi_args.a1 = a1;
	wmi_args.a2 = zone_id;
	wmi_args.a3 = data;

	struct acpi_buffer input = {
		(acpi_size) sizeof(struct casper_wmi_args),
		&wmi_args
	};
	return wmi_set_block(CASPER_WMI_GUID, 0, &input);
}

static ssize_t led_control_show(struct device *dev, struct device_attribute
				*attr, char *buf)
{
	return -EOPNOTSUPP;
}

/**
 * led_control_store() - sysfs store function for direct LED control
 * @dev: Device pointer
 * @attr: Device attribute
 * @buf: Input buffer containing hex value "ZZZZRRGGBB"
 * @count: Buffer size
 *
 * Input format: 64-bit hex value where:
 *   - Upper 32 bits: LED zone ID (3-7)
 *   - Lower 32 bits: RGB color data (0xRRGGBB)
 *
 * Example: "0600FF0000" sets all keyboard LEDs (zone 6) to red
 *
 * Return: Number of bytes written, or negative error code
 */
static ssize_t led_control_store(struct device *dev, struct device_attribute
				 *attr, const char *buf, size_t count)
{
	u64 tmp;
	int ret;

	ret = kstrtou64(buf, 16, &tmp);
	if (ret)
		return ret;

	u32 led_zone = (tmp >> (8 * 4));
	
	ret = casper_set(CASPER_SET_LED, led_zone, (u32) (tmp & 0xFFFFFFFF)
	    );
	if (ACPI_FAILURE(ret)) {
		dev_err(dev, "casper-wmi ACPI status: %d\n", ret);
		return ret;
	}
	if (led_zone != 7) {
		last_keyboard_led_change = (u32) (tmp & 0xFFFFFFFF);
		last_keyboard_led_zone = led_zone;
	}
	return count;
}

static DEVICE_ATTR_RW(led_control);

static struct attribute *casper_kbd_led_attrs[] = {
	&dev_attr_led_control.attr,
	NULL,
};

ATTRIBUTE_GROUPS(casper_kbd_led);

/**
 * set_casper_backlight_brightness() - LED brightness callback
 * @led_cdev: LED class device
 * @brightness: New brightness value (0-2)
 *
 * Sets the keyboard backlight brightness while preserving the current
 * color setting. Brightness is stored in bits 24-31 of the color value.
 */
static void set_casper_backlight_brightness(struct led_classdev *led_cdev,
				     enum led_brightness brightness)
{
	// Setting any of the keyboard leds' brightness sets brightness of all
	acpi_status ret = casper_set(CASPER_SET_LED,
				     CASPER_KEYBOARD_LED_1,
				     (last_keyboard_led_change & 0xF0FFFFFF) |
				     (((u32) brightness) << 24)
	    );

	if (ret != 0)
		dev_err(led_cdev->dev,
			"Couldn't set brightness acpi status: %d\n", ret);
}

/**
 * get_casper_backlight_brightness() - LED brightness get callback
 * @led_cdev: LED class device
 *
 * Returns the current keyboard backlight brightness from cached state.
 * Note: Corner LED brightness is not separately tracked.
 *
 * Return: Current brightness value (0-2)
 */
static enum led_brightness get_casper_backlight_brightness(struct led_classdev
						    *led_cdev)
{
	return (last_keyboard_led_change & 0x0F000000) >> 24;
}

static struct led_classdev casper_kbd_led = {
	.name = "casper::kbd_backlight",
	.brightness = 0,
	.brightness_set = set_casper_backlight_brightness,
	.brightness_get = get_casper_backlight_brightness,
	.max_brightness = 2,
	.groups = casper_kbd_led_groups,
};

/**
 * enum casper_power_plan - Power plan/performance profile values
 * @HIGH_POWER: Maximum performance mode (1)
 * @GAMING: Balanced gaming mode (2)
 * @TEXT_MODE: Quiet operation mode (3)
 * @LOW_POWER: Power saving mode (4)
 *
 * These values are written to the WMI interface to switch between
 * different performance profiles.
 */
enum casper_power_plan {
	HIGH_POWER = 1,
	GAMING = 2,
	TEXT_MODE = 3,
	LOW_POWER = 4
};


/**
 * casper_query() - Query hardware information via WMI
 * @wdev: WMI device
 * @a1: Query type (CASPER_GET_HARDWAREINFO, CASPER_POWERPLAN, etc.)
 * @out: Output structure to fill with response
 *
 * Sends a WMI read command and retrieves the 32-byte response buffer.
 * Used to get fan speeds, power plan status, and other hardware info.
 *
 * Return: ACPI status code
 */
static acpi_status casper_query(struct wmi_device *wdev, u16 a1,
				struct casper_wmi_args *out)
{
	struct casper_wmi_args wmi_args = { 0 };
	wmi_args.a0 = CASPER_READ;
	wmi_args.a1 = a1;

	struct acpi_buffer input = {
		(acpi_size) sizeof(struct casper_wmi_args),
		&wmi_args
	};
	acpi_status ret = wmi_set_block(CASPER_WMI_GUID, 0, &input);
	if (ACPI_FAILURE(ret)) {
		dev_err(&wdev->dev,
			"Could not query (set phase), acpi status: %u", ret);
		return ret;
	}

	union acpi_object *obj = wmidev_block_query(wdev, 0);
	if (obj == NULL) {
		dev_err(&wdev->dev,
			"Could not query (query) hardware information");
		return AE_ERROR;
	}
	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Return type is not a buffer");
		return AE_TYPE;
	}

	if (obj->buffer.length != 32) {
		dev_err(&wdev->dev, "Return buffer is not long enough");
		return AE_ERROR;
	}
	memcpy(out, obj->buffer.pointer, 32);
	kfree(obj);
	return ret;
}

/**
 * casper_power_plan_to_profile() - Convert Casper power plan to platform_profile
 * @plan: Casper power plan value (1-4)
 *
 * Maps Casper-specific power plan values to standard Linux platform_profile
 * enum values.
 *
 * Return: Corresponding platform_profile_option value
 */
static enum platform_profile_option casper_power_plan_to_profile(u32 plan)
{
	switch (plan) {
	case HIGH_POWER:
		return PLATFORM_PROFILE_PERFORMANCE;
	case GAMING:
		return PLATFORM_PROFILE_BALANCED_PERFORMANCE;
	case TEXT_MODE:
		return PLATFORM_PROFILE_QUIET;
	case LOW_POWER:
		return PLATFORM_PROFILE_LOW_POWER;
	default:
		return PLATFORM_PROFILE_BALANCED;
	}
}

/**
 * casper_profile_to_power_plan() - Convert platform_profile to Casper power plan
 * @profile: Linux platform_profile value
 *
 * Maps standard Linux platform_profile enum values to Casper-specific
 * power plan values for WMI communication.
 *
 * Return: Corresponding Casper power plan value (1-4)
 */
static u32 casper_profile_to_power_plan(enum platform_profile_option profile)
{
	switch (profile) {
	case PLATFORM_PROFILE_PERFORMANCE:
		return HIGH_POWER;
	case PLATFORM_PROFILE_BALANCED_PERFORMANCE:
		return GAMING;
	case PLATFORM_PROFILE_QUIET:
		return TEXT_MODE;
	case PLATFORM_PROFILE_LOW_POWER:
		return LOW_POWER;
	case PLATFORM_PROFILE_BALANCED:
		return GAMING;
	case PLATFORM_PROFILE_COOL:
		return LOW_POWER;
	default:
		return GAMING;
	}
}

/**
 * casper_profile_probe() - platform_profile probe callback
 * @drvdata: Driver data pointer (wmi_device)
 * @choices: Bitmap of available profile choices
 *
 * Called when platform_profile device is registered. Sets up the
 * available profile choices for this hardware.
 *
 * Return: 0 on success, negative error code on failure
 */
static int casper_profile_probe(void *drvdata, unsigned long *choices)
{
	set_bit(PLATFORM_PROFILE_LOW_POWER, choices);
	set_bit(PLATFORM_PROFILE_QUIET, choices);
	set_bit(PLATFORM_PROFILE_BALANCED, choices);
	set_bit(PLATFORM_PROFILE_BALANCED_PERFORMANCE, choices);
	set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);
	return 0;
}

/**
 * casper_profile_get() - platform_profile get callback
 * @dev: Platform profile device
 * @profile: Output for current profile
 *
 * Reads the current power plan from the firmware and converts it to
 * platform_profile format.
 *
 * Return: 0 on success, negative error code on failure
 */
static int casper_profile_get(struct device *dev, enum platform_profile_option *profile)
{
	struct wmi_device *wdev = to_wmi_device(dev->parent);
	struct casper_wmi_args out = { 0 };
	acpi_status ret;

	ret = casper_query(wdev, CASPER_POWERPLAN, &out);
	if (ACPI_FAILURE(ret))
		return -EIO;

	*profile = casper_power_plan_to_profile(out.a2);
	return 0;
}

/**
 * casper_profile_set() - platform_profile set callback
 * @dev: Platform profile device
 * @profile: New profile to set
 *
 * Converts the platform_profile value to Casper power plan and writes
 * it to the firmware.
 *
 * Return: 0 on success, negative error code on failure
 */
static int casper_profile_set(struct device *dev, enum platform_profile_option profile)
{
	u32 power_plan = casper_profile_to_power_plan(profile);
	acpi_status ret;

	ret = casper_set(CASPER_POWERPLAN, power_plan, 0);
	if (ACPI_FAILURE(ret)) {
		dev_err(dev, "Couldn't set power plan, acpi_status: %d", ret);
		return -EINVAL;
	}
	return 0;
}

static struct platform_profile_ops casper_platform_profile_ops = {
	.probe = casper_profile_probe,
	.profile_get = casper_profile_get,
	.profile_set = casper_profile_set,
};

/**
 * casper_wmi_hwmon_is_visible() - Check hwmon attribute visibility
 * @drvdata: Driver data
 * @type: Sensor type
 * @attr: Attribute
 * @channel: Channel number
 *
 * Return: File permissions (0444 for read-only fans)
 */
static umode_t casper_wmi_hwmon_is_visible(const void *drvdata,
					   enum hwmon_sensor_types type,
					   u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		return 0444;	// read only
	default:
		return 0;
	}
	return 0;
}

/**
 * casper_wmi_hwmon_read() - Read hwmon sensor values
 * @dev: Device
 * @type: Sensor type (hwmon_fan)
 * @attr: Attribute
 * @channel: Channel (0=CPU, 1=GPU for fans)
 * @val: Output value pointer
 *
 * Reads fan speeds (RPM).
 *
 * Return: 0 on success, negative error code on failure
 */
static int casper_wmi_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *val)
{
	struct casper_wmi_args out = { 0 };
	switch (type) {
	case hwmon_fan:
		acpi_status ret = casper_query(to_wmi_device(dev->parent),
					       CASPER_GET_HARDWAREINFO, &out);
		if (ACPI_FAILURE(ret))
			return ret;

		if (channel == 0) {	// CPU fan
			u16 cpu_fanspeed = (u16) out.a4;
                        if (!(*casper_raw_fanspeed)) {
                                cpu_fanspeed <<= (u16) 8;
                                cpu_fanspeed += (u16) (out.a4 >> 8);
                        }
			*val = cpu_fanspeed;
		} else if (channel == 1) {	// GPU fan
			u16 gpu_fanspeed = (u16) out.a5;
                        if (!(*casper_raw_fanspeed)) {
                                gpu_fanspeed <<= (u16) 8;
                                gpu_fanspeed += (u16) (out.a5 >> 8);
                        }
			*val = gpu_fanspeed;
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * casper_wmi_hwmon_read_string() - Get hwmon sensor labels
 * @dev: Device
 * @type: Sensor type
 * @attr: Attribute
 * @channel: Channel
 * @str: Output string pointer
 *
 * Provides labels for fan channels.
 *
 * Return: 0 on success, -EOPNOTSUPP if not implemented
 */
static int casper_wmi_hwmon_read_string(struct device *dev,
				 enum hwmon_sensor_types type, u32 attr,
				 int channel, const char **str)
{
	switch (type) {
	case hwmon_fan:
		switch (channel) {
		case 0:
			*str = "cpu_fan_speed";
			break;
		case 1:
			*str = "gpu_fan_speed";
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static struct hwmon_ops casper_wmi_hwmon_ops = {
	.is_visible = &casper_wmi_hwmon_is_visible,
	.read = &casper_wmi_hwmon_read,
	.read_string = &casper_wmi_hwmon_read_string,
};

static const struct hwmon_channel_info *const casper_wmi_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	NULL
};

static const struct hwmon_chip_info casper_wmi_hwmon_chip_info = {
	.ops = &casper_wmi_hwmon_ops,
	.info = casper_wmi_hwmon_info,
};

/**
 * casper_wmi_probe() - Probe function for WMI device
 * @wdev: WMI device
 * @context: Driver context
 *
 * Initializes the driver, performs DMI matching, and registers
 * LED, hwmon, and platform_profile subsystems.
 *
 * Return: 0 on success, negative error code on failure
 */
static int casper_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct device *hwmon_dev;
	int ret;

	// All Casper Excalibur Laptops use this GUID
	if (!wmi_has_guid(CASPER_WMI_GUID))
		return -ENODEV;

	dmi_check_system(casper_dmi_list);
        
        if (casper_raw_fanspeed) {
                // This is to add their BIOS version to the dmi list
                dev_warn(&wdev->dev,
			 "If you are using an intel CPU older than 10th gen, contact driver maintainer.");
        }

	// Register hwmon for fan monitoring
	hwmon_dev = devm_hwmon_device_register_with_info(&wdev->dev, "casper_wmi", wdev,
						 &casper_wmi_hwmon_chip_info,
						 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	// Register LED device
	ret = led_classdev_register(&wdev->dev, &casper_kbd_led);
	if (ret != 0)
		return ret;

	// Register platform_profile for power plan control
	hwmon_dev = devm_platform_profile_register(&wdev->dev, "casper-wmi", wdev,
						  &casper_platform_profile_ops);
	if (IS_ERR(hwmon_dev)) {
		dev_err(&wdev->dev, "Failed to register platform_profile\n");
		led_classdev_unregister(&casper_kbd_led);
		return PTR_ERR(hwmon_dev);
	}

	return 0;
}

/**
 * casper_wmi_remove() - Remove callback for WMI device
 * @wdev: WMI device
 *
 * Unregisters the LED and platform_profile devices when the driver is unloaded.
 */
static void casper_wmi_remove(struct wmi_device *wdev)
{
	platform_profile_remove(&wdev->dev);
	led_classdev_unregister(&casper_kbd_led);
}


static const struct wmi_device_id casper_wmi_id_table[] = {
	{ CASPER_WMI_GUID, NULL },
	{ }
};

static struct wmi_driver casper_wmi_driver = {
	.driver = {
		   .name = "casper-wmi",
		    },
	.id_table = casper_wmi_id_table,
	.probe = casper_wmi_probe,
	.remove = &casper_wmi_remove
};

module_wmi_driver(casper_wmi_driver);

MODULE_DEVICE_TABLE(wmi, casper_wmi_id_table);