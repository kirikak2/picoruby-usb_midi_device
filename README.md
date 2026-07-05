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

## Notes

- ESP32 only at present (ESP32-P4 / M5Stack Tab5 via TinyUSB). Other
  ports are planned.
- Singleton: only one USB MIDI device instance exists per build.
- Hot-plug aware — `connected?` reflects the host enumeration state.

## License

MIT
