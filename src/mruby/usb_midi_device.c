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

void
mrb_picoruby_usb_midi_device_gem_init(mrb_state *mrb)
{
    struct RClass *cls = mrb_define_class_id(mrb, MRB_SYM(USB_MIDI_DEVICE), mrb->object_class);

    mrb_define_method_id(mrb, cls, MRB_SYM(_init),            mrb_usb_midi_device_init,            MRB_ARGS_NONE());
    mrb_define_method_id(mrb, cls, MRB_SYM(_connected),       mrb_usb_midi_device_connected,       MRB_ARGS_NONE());
    mrb_define_method_id(mrb, cls, MRB_SYM(_send_packet),     mrb_usb_midi_device_send_packet,     MRB_ARGS_REQ(5));
    mrb_define_method_id(mrb, cls, MRB_SYM(_bytes_available), mrb_usb_midi_device_bytes_available, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, cls, MRB_SYM(_read_available),  mrb_usb_midi_device_read_available,  MRB_ARGS_NONE());
}

void
mrb_picoruby_usb_midi_device_gem_final(mrb_state *mrb)
{
    /* The USB device stays up across scripts (the host keeps it enumerated). */
}
