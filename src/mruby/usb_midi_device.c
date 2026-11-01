/*
 * PicoRuby USB-MIDI Device - mruby bindings
 */

#include <mruby.h>
#include <mruby/presym.h>
#include <mruby/class.h>
#include <mruby/string.h>

#include "../../include/usb_midi_device.h"

/* USB_MIDI_DEVICE._init */
static mrb_value
mrb_usb_midi_device_init(mrb_state *mrb, mrb_value self)
{
    int ret = USB_MIDI_DEVICE_init();
    return mrb_fixnum_value(ret);
}

/* USB_MIDI_DEVICE._connected */
static mrb_value
mrb_usb_midi_device_connected(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(USB_MIDI_DEVICE_connected());
}

/* USB_MIDI_DEVICE._send_packet(cable, cin, midi1, midi2, midi3) */
static mrb_value
mrb_usb_midi_device_send_packet(mrb_state *mrb, mrb_value self)
{
    mrb_int cable, cin, midi1, midi2, midi3;
    mrb_get_args(mrb, "iiiii", &cable, &cin, &midi1, &midi2, &midi3);

    int ret = USB_MIDI_DEVICE_send_packet((uint8_t)cable, (uint8_t)cin,
                                          (uint8_t)midi1, (uint8_t)midi2,
                                          (uint8_t)midi3);
    return mrb_fixnum_value(ret);
}

/* USB_MIDI_DEVICE._bytes_available */
static mrb_value
mrb_usb_midi_device_bytes_available(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(USB_MIDI_DEVICE_bytes_available());
}

/* USB_MIDI_DEVICE._read_available */
static mrb_value
mrb_usb_midi_device_read_available(mrb_state *mrb, mrb_value self)
{
    int available = USB_MIDI_DEVICE_bytes_available();
    if (available < 4) {
        return mrb_nil_value();
    }

    uint8_t buffer[64];
    size_t max_read = (available > 64) ? 64 : available;

    int read_len = USB_MIDI_DEVICE_read_packet(buffer, max_read);
    if (read_len <= 0) {
        return mrb_nil_value();
    }

    return mrb_str_new(mrb, (const char *)buffer, read_len);
}

/*
 * For the picoruby-midibase transport (mrblib_midibase/): MIDIBASE parses a
 * byte stream and encodes whole messages, USB-MIDI carries 4-byte packets.
 */

/* MIDI bytes a packet carries, by Code Index Number (0 / 1: reserved) */
static const uint8_t k_cin_length[16] = {
    0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1
};

/* What is left of the packet _read_byte is handing out */
static uint8_t s_rx_bytes[3];
static uint8_t s_rx_len, s_rx_pos;

/* USB_MIDI_DEVICE#_read_byte -> the next MIDI byte from the host, or nil */
static mrb_value
mrb_usb_midi_device_read_byte(mrb_state *mrb, mrb_value self)
{
    while (s_rx_pos >= s_rx_len) {
        uint8_t packet[4];
        if (USB_MIDI_DEVICE_read_packet(packet, 4) < 4) {
            return mrb_nil_value();
        }
        s_rx_len = k_cin_length[packet[0] & 0x0F];
        s_rx_pos = 0;
        s_rx_bytes[0] = packet[1];
        s_rx_bytes[1] = packet[2];
        s_rx_bytes[2] = packet[3];
    }
    return mrb_fixnum_value(s_rx_bytes[s_rx_pos++]);
}

static int
send3(uint8_t cin, const uint8_t *b, int n)
{
    return USB_MIDI_DEVICE_send_packet(0, cin, b[0], n > 1 ? b[1] : 0, n > 2 ? b[2] : 0);
}

/* USB_MIDI_DEVICE#_write_message(bytes) -> bytes sent, or -1
 * `bytes` is one whole message as MIDIBASE.encode makes it. */
static mrb_value
mrb_usb_midi_device_write_message(mrb_state *mrb, mrb_value self)
{
    mrb_value str;
    mrb_get_args(mrb, "S", &str);
    const uint8_t *b = (const uint8_t *)RSTRING_PTR(str);
    int n = (int)RSTRING_LEN(str);
    if (n == 0) return mrb_fixnum_value(0);

    uint8_t status = b[0];
    int ret = 0;
    if (status == 0xF0) {
        /* SysEx: three bytes a packet, the last one says how many end it */
        int i = 0;
        while (ret == 0 && i < n) {
            int left = n - i;
            if (left > 3) {
                ret = send3(0x4, b + i, 3);
                i += 3;
            } else {
                ret = send3((uint8_t)(0x4 + left), b + i, left);   /* 5 / 6 / 7 */
                i = n;
            }
        }
    } else if (0x80 <= status && status < 0xF0) {
        ret = send3(status >> 4, b, n);
    } else {
        switch (status) {
        case 0xF1: case 0xF3: ret = send3(0x2, b, n); break;   /* 2-byte System Common */
        case 0xF2:            ret = send3(0x3, b, n); break;   /* Song Position */
        case 0xF6:            ret = send3(0x5, b, n); break;   /* Tune Request */
        default:   /* realtime, or anything else: a byte a packet */
            for (int i = 0; ret == 0 && i < n; i++) ret = send3(0xF, b + i, 1);
            break;
        }
    }
    return mrb_fixnum_value(ret == 0 ? n : -1);
}

void
mrb_picoruby_usb_midi_device_gem_init(mrb_state *mrb)
{
    struct RClass *cls = mrb_define_class_id(mrb, MRB_SYM(USB_MIDI_DEVICE), mrb->object_class);

    mrb_define_method_id(mrb, cls, MRB_SYM(_init),            mrb_usb_midi_device_init,            MRB_ARGS_NONE());
    mrb_define_method_id(mrb, cls, MRB_SYM(_connected),       mrb_usb_midi_device_connected,       MRB_ARGS_NONE());
    mrb_define_method_id(mrb, cls, MRB_SYM(_send_packet),     mrb_usb_midi_device_send_packet,     MRB_ARGS_REQ(5));
    mrb_define_method_id(mrb, cls, MRB_SYM(_bytes_available), mrb_usb_midi_device_bytes_available, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, cls, MRB_SYM(_read_available),  mrb_usb_midi_device_read_available,  MRB_ARGS_NONE());
    mrb_define_method_id(mrb, cls, MRB_SYM(_read_byte),       mrb_usb_midi_device_read_byte,       MRB_ARGS_NONE());
    mrb_define_method_id(mrb, cls, MRB_SYM(_write_message),   mrb_usb_midi_device_write_message,   MRB_ARGS_REQ(1));
}

void
mrb_picoruby_usb_midi_device_gem_final(mrb_state *mrb)
{
    /* The USB device stays up across scripts (the host keeps it enumerated). */
}
