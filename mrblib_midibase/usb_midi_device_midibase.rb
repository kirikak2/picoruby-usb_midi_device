# USB_MIDI_DEVICE as a picoruby-midibase transport, like UART::MIDI:
# getevent / putevent / handle, so it can be a MIDIBASE::Router source and
# sink and play a sound generator that takes MIDIBASE events (PSG::Synth).
#
# Not in mrblib/: a build without picoruby-midibase (MIDoRI, which uses
# picoruby-midi) has no MIDIBASE to include. A build that has it adds this
# file in its build config:
#
#   conf.gem core: 'picoruby-usb_midi_device' do |spec|
#     spec.add_dependency 'picoruby-midibase'
#     spec.rbfiles << "#{spec.dir}/mrblib_midibase/usb_midi_device_midibase.rb"
#   end
#
# getevent and read_available both take from the same buffer of what the
# host sent: use one or the other.
#
#   usb = USB_MIDI_DEVICE.instance
#   router = MIDIBASE::Router.new
#   router.connect(:usb, synth)
#   Task.new { loop { router.emit(:usb, usb.getevent) } }
class USB_MIDI_DEVICE
  include ::MIDIBASE

  def initialize(time_signature: [4, 4], max_sysex_bytes: ::MIDIBASE::DEFAULT_MAX_SYSEX_BYTES)
    _init
    initialize_midibase(time_signature: time_signature, max_sysex_bytes: max_sysex_bytes)
  end

  # One message to the host. USB-MIDI sends whole messages in packets, so
  # this encodes the message and packs it at once, instead of writing it a
  # byte at a time as MIDIBASE#putevent does.
  # @return [Integer, false] bytes sent, or false (not connected / buffer full)
  def putevent(command, *values)
    encoded = ::MIDIBASE.encode(command, *values)
    size = encoded.size
    bytes = "\0" * size
    i = 0
    while i < size
      bytes.setbyte(i, encoded[i])
      i += 1
    end
    _write_message(bytes) < 0 ? false : size
  end

  private def midi_read_byte
    _read_byte
  end

  # A lone byte, as USB-MIDI's single-byte packet (CIN 0xF). putevent does
  # not come here.
  private def midi_write_byte(byte)
    _send_packet(0, 0x0F, byte, 0, 0)
  end
end
