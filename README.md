# Casper Excalibur Laptop WMI Driver

> ⚠️ **Deprecated:** This out-of-tree module is no longer actively maintained.
> The preferred solution is the in-kernel patch — track progress at:
> https://lore.kernel.org/platform-driver-x86/?q=casper-wmi
>
> This module may work for basic functionality but has known limitations.
> G870 support added in this fork — DMI matching and platform_profile work,
> but RGB color control via led_control is not yet confirmed working.

Linux kernel driver for Casper Excalibur gaming laptops providing keyboard backlight control, fan monitoring, and power profile management.

## Features

| Feature | Interface | Description |
|---------|-----------|-------------|
| Keyboard Backlight | LED subsystem | Brightness (0-2) and RGB color control |
| Fan Monitoring | HWMON | CPU and GPU fan speed (RPM) |
| Power Profiles | platform_profile | Performance profile switching |

## Supported Hardware

| Model | Vendor | Notes |
|-------|--------|-------|
| EXCALIBUR G650 | CASPER BILGISAYAR SISTEMLERI | |
| EXCALIBUR G750 | CASPER BILGISAYAR SISTEMLERI | |
| EXCALIBUR G670 | CASPER BILGISAYAR SISTEMLERI | |
| EXCALIBUR G870 | CASPER BILGISAYAR SISTEMLERI | |
| EXCALIBUR G900 | CASPER BILGISAYAR SISTEMLERI | BIOS CP131 |

## Building

```bash
make
sudo insmod casper-wmi.ko
```

To install permanently:
```bash
sudo make install
sudo depmod -a
```

## Usage

### Keyboard Backlight

```bash
# Brightness (0=off, 1=low, 2=high)
echo 2 | sudo tee /sys/class/leds/casper::kbd_backlight/brightness

# RGB color (format: ZZZZRRGGBB where Z=zone)
echo 0600FF0000 | sudo tee /sys/class/leds/casper::kbd_backlight/led_control
```

Zones: 3, 4, 5 (keyboard zones), 6 (all), 7 (corner LEDs)

### Fan Speeds

```bash
cat /sys/class/hwmon/hwmon*/fan1_input  # CPU
cat /sys/class/hwmon/hwmon*/fan2_input  # GPU
```

### Power Profiles

```bash
cat /sys/firmware/acpi/platform_profile
echo performance | sudo tee /sys/firmware/acpi/platform_profile
```

Available profiles: `performance`, `balanced_performance`, `quiet`, `low-power`

## Technical Details

**WMI GUID:** `644C5791-B7B0-4123-A90B-E93876E0DAAD`

**Power Plan Mapping:**

| Casper | platform_profile |
|--------|------------------|
| HIGH_POWER (1) | performance |
| GAMING (2) | balanced_performance |
| TEXT_MODE (3) | quiet |
| LOW_POWER (4) | low-power |

## License

GPL v2

## Maintainer

Mustafa Ekşi <mustafa.eskieksi@gmail.com>
