# picoruby-usb_midi_device

USB-MIDI Device transport layer for PicoRuby.

Turns the microcontroller into a USB MIDI *device*: it enumerates on a
host PC (or tablet) as a standard USB-MIDI class-compliant instrument,
sends MIDI events to the host (device → host), and receives MIDI events
from the host (host → device). The ESP-IDF / TinyUSB device stack is
bundled inside the gem (see `ports/esp32/`) so the gem alone is
sufficient — the host application only needs to call the public API.

This is the device-side counterpart to
[`picoruby-usb_midi_host`](https://github.com/kirikak2/picoruby-usb_midi_host).

## Usage

```ruby
require 'midi'
require 'usb_midi_device'

usb = USB_MIDI_DEVICE.instance
puts "Waiting for USB host..."
sleep 0.1 until usb.connected?

device = MIDI::Device.new(usb)
device.note_on(60, 100)
sleep 1
device.note_off(60)
```

Receive MIDI events from the host:

```ruby
input = MIDI::Input.new(device)
input.on(:note_on)  { |e| puts "note on  #{e[:note]} vel=#{e[:velocity]}" }
input.on(:note_off) { |e| puts "note off #{e[:note]}" }
input.start
loop do
  input.process
  sleep_ms 5
end
```

## API

### Methods

- `USB_MIDI_DEVICE.instance` - Singleton handle. Lazily initializes the
  USB device stack on first call.
- `transport_id` - Returns 4 (`MIDI_TRANSPORT_USB_DEVICE`, a distinct
  bit from USB host `0x01` and SAM2695 `0x02`).
- `connected?` - `true` once the host PC has enumerated this device.
- `send_packet(cable, cin, midi1, midi2, midi3)` - Send one USB-MIDI
  4-byte packet (device → host).
- `bytes_available` - Number of bytes queued from the host (host →
  device).
- `read_available` - Binary String of buffered 4-byte USB-MIDI packets,
  or nil if none.

## Build

The snippets below are what a project *other than* Midori needs in order
to use this gem. All of it is ESP-IDF side wiring: the gem builds no
ESP32 code by itself.

### 1. Add the gem to the PicoRuby build config

```ruby
# build_config/xtensa-esp.rb (ESP32-S3) / build_config/riscv-esp.rb (ESP32-P4)
conf.gem github: 'kirikak2/picoruby-usb_midi_device'
# ...or, when vendored under mrbgems/
conf.gem core: 'picoruby-usb_midi_device'
```

`picoruby-machine` is pulled in automatically. `picoruby-midi` is not a
hard dependency — add it only if you want the `MIDI::Device` /
`MIDI::Input` layer shown above.

### 2. Depend on esp_tinyusb 2.x

```yaml
# idf_component.yml
dependencies:
  espressif/esp_tinyusb:
    version: ">=2.0.0"
```

The port uses the run-time configuration API introduced in esp_tinyusb
2.0 (`tinyusb_config_t.port` / `.phy` / `.task` / `.descriptor`).
esp_tinyusb 1.x will not compile.

### 3. Compile the ESP32 port sources

`mrbgem.rake` builds only `src/` and `mrblib/`; the `ports/esp32/` files
must be added to the ESP-IDF component that hosts PicoRuby:

```cmake
set(GEM_DIR ${COMPONENT_DIR}/path/to/picoruby-usb_midi_device)

idf_component_register(
  SRCS
    # ...
    ${GEM_DIR}/ports/esp32/usb_midi_device.c
    ${GEM_DIR}/ports/esp32/usb_descriptors.c
  INCLUDE_DIRS
    # ...
    ${GEM_DIR}/include
  PRIV_REQUIRES
    # ...
    esp_tinyusb
    esp_hw_support   # ESP32-P4 only (hal/usb_serial_jtag_ll.h, PHY mux)
)
```

### 4. Define `CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE`

Both port files are wrapped in `#ifdef
CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE`. Without it they still link, but
as stubs (`USB_MIDI_DEVICE_start()` returns -1, `connected?` is always
false). The gem ships no Kconfig of its own, so the project declares the
symbol — e.g. in `main/Kconfig.projbuild`:

```kconfig
config USB_MIDI_USB_MODE_MIDI_DEVICE
    bool "USB port acts as a TinyUSB CDC + MIDI device"
    default y
```

(Midori declares it as one arm of a `choice` over USB port roles; a plain
`config` is enough when the USB port is always the MIDI device.)

### 5. sdkconfig.defaults

```
CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE=y

# TinyUSB device stack: CDC + MIDI composite
CONFIG_TINYUSB_CDC_ENABLED=y
CONFIG_TINYUSB_MIDI_COUNT=1

# USB-Serial/JTAG must not fight TinyUSB for the PHY: keep the primary
# console on UART and disable the secondary USB-Serial/JTAG console.
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
```

Do **not** select `CONFIG_ESP_CONSOLE_USB_CDC` or
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`: the port calls
`tinyusb_console_init()` at start-up to move `ESP_LOG` output onto its
own CDC interface.

### 6. Start the driver from `app_main()`

`USB_MIDI_DEVICE.instance` only allocates the RX ring buffer. Installing
TinyUSB is a C-side entry point that the application must call before the
Ruby VM starts:

```c
#include "usb_midi_device.h"

void app_main(void)
{
#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
    USB_MIDI_DEVICE_start();  /* TinyUSB + CDC console + TX task */
#endif
    /* ...start the PicoRuby VM task... */
}
```

### Target-specific behaviour

- **ESP32-S3** — one internal PHY is shared by USB-OTG and
  USB-Serial/JTAG, so a USB-MIDI *host* cannot coexist with this gem in
  the same build. ESP-IDF's `usb_phy` driver performs the hand-over
  (`phy.skip_setup = false`); nothing extra to configure.
- **ESP32-P4** — `USB_MIDI_DEVICE_start()` swaps the FSLS PHY mux
  (`usb_serial_jtag_ll_phy_select(1)`) so OTG1.1 drives the USB-C pads.
  The HS OTG port (USB-A on the Tab5) is untouched and can still run a
  USB host driver.
- On both targets USB-Serial/JTAG is disconnected from the connector once
  the driver starts: `idf.py flash` then needs manual download mode (hold
  BOOT, tap RESET), while `idf.py monitor` works over the TinyUSB CDC
  port.
- The TX task is pinned to core 1, the same core as TinyUSB's device
  task, because `tud_midi_packet_write()` must never run in true parallel
  with `tud_task()`. If the PicoRuby VM task runs on a different core,
  leave these pinnings alone — sends are already funnelled through a
  queue.

### Customising the USB identity

VID/PID, product/manufacturer strings and the endpoint layout are
hard-coded in `ports/esp32/usb_descriptors.c` (currently `0x303A`,
Espressif's VID, with "Midori" strings). Edit that file for your own
product identity.

## Notes

- ESP32 only at present (ESP32-P4 / M5Stack Tab5, ESP32-S3 via TinyUSB).
  Other ports are planned.
- Singleton: only one USB MIDI device instance exists per build.
- Hot-plug aware — `connected?` reflects the host enumeration state.

## License

MIT
