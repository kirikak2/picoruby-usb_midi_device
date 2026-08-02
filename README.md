# picoruby-usb_midi_device

USB-MIDI Device transport layer for PicoRuby.

Turns the microcontroller into a USB MIDI *device*: it enumerates on a
host PC (or tablet) as a standard USB-MIDI class-compliant instrument,
sends MIDI events to the host (device → host), and receives MIDI events
from the host (host → device). The ESP-IDF / TinyUSB device stack is
bundled inside the gem (see `ports/esp32/`) so the gem alone is
sufficient — the host application only needs to call the public API.

The descriptors are MIDI-only and carry no product identity of their own:
everything a project would want to change (VID/PID, manufacturer/product
strings, an optional CDC interface, task placement) is a build-time define
listed in [`include/usb_midi_device_config.h`](include/usb_midi_device_config.h).

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

### C API

Beyond the Ruby bindings the port exposes `usb_midi_device.h`:

- `USB_MIDI_DEVICE_start()` - install TinyUSB and start the device.
- `USB_MIDI_DEVICE_connected()` / `_send_packet()` / `_bytes_available()`
  / `_read_packet()` - what the Ruby methods above wrap.
- `USB_MIDI_DEVICE_set_cdc_rx_callback(cb, arg)` - CDC-only, see below.

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

### 4. Enable the port

Both port files are wrapped in `#if USB_MIDI_DEVICE_ENABLED`, which is 0
unless the project says otherwise. Without it they still link, but as
stubs (`USB_MIDI_DEVICE_start()` returns -1, `connected?` is always
false), so the gem can stay in a build that also supports non-device USB
modes. Enable it with a define:

```cmake
target_compile_definitions(${COMPONENT_LIB} PRIVATE USB_MIDI_DEVICE_ENABLED=1)
```

or, if you prefer a menuconfig switch, declare the gem-namespaced symbol
in your own Kconfig — the config header picks it up automatically:

```kconfig
config USB_MIDI_DEVICE_ENABLED
    bool "USB port acts as a TinyUSB MIDI device"
    default y

config USB_MIDI_DEVICE_WITH_CDC
    bool "Add a CDC-ACM interface next to MIDI"
    default n
```

(Midori derives both from a `choice` over USB port roles and injects them
from CMake.)

### 5. sdkconfig.defaults

```
# TinyUSB device stack
CONFIG_TINYUSB_MIDI_COUNT=1
# only when building with USB_MIDI_DEVICE_WITH_CDC=1
CONFIG_TINYUSB_CDC_ENABLED=y

# USB-Serial/JTAG must not fight TinyUSB for the PHY: keep the primary
# console on UART and disable the secondary USB-Serial/JTAG console.
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
```

Do **not** select `CONFIG_ESP_CONSOLE_USB_CDC` or
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`: with CDC enabled the port calls
`tinyusb_console_init()` at start-up to move `ESP_LOG` output onto its
own CDC interface, and in a MIDI-only build there is no USB console at
all — keep it on UART.

### 6. Start the driver from `app_main()`

`USB_MIDI_DEVICE.instance` only allocates the RX ring buffer. Installing
TinyUSB is a C-side entry point that the application must call before the
Ruby VM starts:

```c
#include "usb_midi_device.h"

void app_main(void)
{
    USB_MIDI_DEVICE_start();  /* TinyUSB + TX task (+ CDC when enabled);
                               * returns -1 in a stubbed-out build */
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

Nothing product-specific is hard-coded in `ports/esp32/usb_descriptors.c`.
Every value has a neutral default in `include/usb_midi_device_config.h`
and is overridden with a define, either per flag

```cmake
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  USB_MIDI_DEVICE_MANUFACTURER=\"Acme Instruments\"
  USB_MIDI_DEVICE_PRODUCT=\"Acme Groovebox\"
  USB_MIDI_DEVICE_SERIAL=\"ACME-0001\"
  USB_MIDI_DEVICE_VID=0x1234
  USB_MIDI_DEVICE_PID=0x5678
)
```

or by pointing the gem at a header of your own:

```cmake
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  USB_MIDI_DEVICE_CONFIG_HEADER=\"acme_usb_identity.h\")
```

| Define | Default | Meaning |
|---|---|---|
| `USB_MIDI_DEVICE_ENABLED` | 0 (1 if `CONFIG_USB_MIDI_DEVICE_ENABLED`) | Master switch; 0 compiles the port to stubs |
| `USB_MIDI_DEVICE_WITH_CDC` | 0 (1 if `CONFIG_USB_MIDI_DEVICE_WITH_CDC`) | Add a CDC-ACM interface (composite device) |
| `USB_MIDI_DEVICE_CDC_CONSOLE` | = `WITH_CDC` | Redirect stdout / `ESP_LOG` to that CDC port |
| `USB_MIDI_DEVICE_VID` | `0x303A` (Espressif) | Vendor ID |
| `USB_MIDI_DEVICE_PID` | `0x4008` / `0x4009` with CDC | Product ID |
| `USB_MIDI_DEVICE_MANUFACTURER` | `"PicoRuby"` | String descriptor 1 |
| `USB_MIDI_DEVICE_PRODUCT` | `"PicoRuby MIDI"` | String descriptor 2 |
| `USB_MIDI_DEVICE_SERIAL` | `"000000000001"` | String descriptor 3 |
| `USB_MIDI_DEVICE_MIDI_ITF_NAME` | = `PRODUCT` | MIDI interface name |
| `USB_MIDI_DEVICE_CDC_ITF_NAME` | `PRODUCT " CDC"` | CDC interface name |
| `USB_MIDI_DEVICE_EPNUM_MIDI_IN` / `_OUT` | `0x81` / `0x01` (`0x83` / `0x03` with CDC) | MIDI endpoints |
| `USB_MIDI_DEVICE_TASK_CORE` | 1 | Core for the TinyUSB and TX tasks |
| `USB_MIDI_DEVICE_TUSB_TASK_*`, `_TX_TASK_*`, `_TX_QUEUE_DEPTH` | see header | Task stack / priority / queue depth |
| `USB_MIDI_DEVICE_P4_PHY_SWAP` | 1 | ESP32-P4 only: swap the FSLS PHY mux to OTG1.1 |

### Optional CDC interface

By default the device exposes a single MIDI function. Set
`USB_MIDI_DEVICE_WITH_CDC=1` to add a CDC-ACM interface — useful when the
same connector must also carry a console or a control protocol. This
changes the device class to IAD/composite, shifts the MIDI endpoints and
bumps the PID, so hosts re-enumerate it as a different device. It also
requires `CONFIG_TINYUSB_CDC_ENABLED=y`.

The gem never interprets CDC traffic itself; it hands received bytes to a
callback the application registers:

```c
#include "usb_midi_device.h"

static void console_rx(const uint8_t *data, size_t len, void *arg)
{
    /* runs in the TinyUSB task - keep it short, copy what you keep */
    for (size_t i = 0; i < len; i++) console_feed_byte(data[i]);
}

USB_MIDI_DEVICE_set_cdc_rx_callback(console_rx, NULL);
```

Registration works before or after `USB_MIDI_DEVICE_start()`; passing
`NULL` unregisters. On a build without CDC the call returns -1 and does
nothing. With `USB_MIDI_DEVICE_CDC_CONSOLE=1` (the default when CDC is on)
the port also routes `ESP_LOG`/stdout to the same interface, so the
callback sees host→device bytes while logs flow the other way.

## Notes

- ESP32 only at present (ESP32-P4 / M5Stack Tab5, ESP32-S3 via TinyUSB).
  Other ports are planned.
- Singleton: only one USB MIDI device instance exists per build.
- Hot-plug aware — `connected?` reflects the host enumeration state.

## License

MIT
