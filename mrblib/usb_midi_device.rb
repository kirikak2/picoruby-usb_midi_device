# USB-MIDI Device transport layer
#
# Provides low-level USB MIDI communication when ESP32 acts as a USB MIDI device.
# Use picoruby-midi for high-level MIDI operations.
#
class USB_MIDI_DEVICE
  def self.instance
    $__usb_midi_device_instance__ = new if $__usb_midi_device_instance__.nil?
    $__usb_midi_device_instance__
  end

  def initialize
    _init
  end

  # Identifier for picoruby-midi transport-mask dispatch.
  # Must be a distinct BIT: USB host = 0x01, SAM2695 = 0x02,
  # USB device = 0x04 (matches MIDI_TRANSPORT_USB_DEVICE in midi.h).
  def transport_id
    4
  end

  # Returns true when host PC has enumerated this device.
  def connected?
    _connected
  end

  # Send a USB-MIDI packet (device → host, MIDI IN jack on host side).
  # @param cable [Integer] Cable number (0-15)
  # @param cin   [Integer] Code Index Number (CIN_* constants)
  # @param midi1 [Integer] First MIDI byte
  # @param midi2 [Integer] Second MIDI byte
  # @param midi3 [Integer] Third MIDI byte
  # @return [Integer] 0 on success, -1 on error
  def send_packet(cable, cin, midi1, midi2, midi3)
    _send_packet(cable, cin, midi1, midi2, midi3)
  end

  # Get number of bytes available from host (host → device, MIDI OUT jack).
  def bytes_available
    _bytes_available
  end

  # Read available MIDI packets from host.
  # Returns binary String containing 4-byte USB-MIDI packets, or nil if none.
  def read_available
    _read_available
  end
end
