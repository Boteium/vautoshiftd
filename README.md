# VAutoShiftD

**Virtual AutoShift Daemon**  
**Make your dumb keyboard feel as smart as QMK.**

`vautoshiftd` makes typing feel easier and more comfortable, especially for your pinky finger. If you get finger or wrist pain from repetitive typing, that may be **RSI** (Repetitive Strain Injury): stress caused by doing the same small movement over and over.

For capitalization and common symbols/hotkeys, your pinky often does extra work reaching for Shift. Auto Shift reduces that: instead of pressing Shift, hold a key a little longer to type the capital letter or shifted symbol. Normally, this kind of feature means buying a QMK-capable keyboard, but you cannot swap your laptop's built-in keyboard for a QMK one. That is where `vautoshiftd` comes to the rescue - you get QMK-style Auto Shift on regular keyboards, including laptop keyboards.

## Features

- autoshift keys: `alphanumeric characters` and `` `- = \ [ ] ; ' , . / `` 
- allow list / deny list keyboard filtering
- keyboard hotplug
- systemd integration

## Quick Start

Build a single portable binary:

```bash
make
```

Run it directly:

```bash
sudo ./vautoshiftd
```

Default behavior is allow-all keyboards with the default autoshift timeout (`175ms`).

## Systemd

Install system-wide:

```bash
sudo make install
```

Enable at boot and start now:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now vautoshiftd.service
```

To change the default 175ms timeout, please edit `/etc/default/vautoshiftd`

```bash
VAUTOSHIFTD_ARGS='--autoshift-timeout=175'
```

## Advanced Usage

List keyboard names:

```bash
/usr/lib/vautoshiftd/vautoshiftd -l
```

Example output:

```text
Logitech_USB_Receiver	/dev/input/event6
Keychron_K2_Max_Keyboard	/dev/input/event8
```

Example usage:

```bash
VAUTOSHIFTD_ARGS="--allow=Logitech_USB_Receiver --allow=Keychron_K2_Max_Keyboard --autoshift-timeout=175"
```

Parameter list:

- `--allow=<name>` (repeatable): allow list mode (only listed keyboards are used)
- `--deny=<name>` (repeatable): deny list mode (all keyboards except listed names)
- `--disable-hotplug`: only keyboards matched at startup are used (no hotplug attach)
- `--autoshift-timeout=<ms>`: autoshift threshold in milliseconds (default: `175`)
- `-l`, `--list-keyboards`: list detected keyboard names and exit

`--allow` and `--deny` are mutually exclusive. If both are provided, `vautoshiftd` exits with an error.

If neither is provided, `vautoshiftd` allows all keyboards.

## Note

This project is a stripped-down, autoshift-focused alternative to keyd. While keyd can be configured to approximate autoshift behavior, that setup can introduce hotkey quirks (for example, modifiers must be released last). `vautoshiftd` is optimized for speed and predictable autoshift behavior, and when a key is autoshifted and kept held, it sends only one shifted character instead of repeating, matching QMK-style behavior.
