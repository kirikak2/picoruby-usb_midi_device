/*
 * PicoRuby USB-MIDI Device - mrubyc bindings
 */

#include <mrubyc.h>
#include <alloc.h>

#include "../../include/usb_midi_device.h"

/* USB_MIDI_DEVICE._init */
static void
c_usb_midi_device_init(mrbc_vm *vm, mrbc_value v[], int argc)
{
    int ret = USB_MIDI_DEVICE_init();
    SET_INT_RETURN(ret);
}

/* USB_MIDI_DEVICE._connected */
static void
c_usb_midi_device_connected(mrbc_vm *vm, mrbc_value v[], int argc)
{
    bool connected = USB_MIDI_DEVICE_connected();
    if (connected) {
        SET_TRUE_RETURN();
    } else {
        SET_FALSE_RETURN();
    }
}

/* USB_MIDI_DEVICE._send_packet(cable, cin, midi1, midi2, midi3) */
static void
c_usb_midi_device_send_packet(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc != 5) {
        SET_INT_RETURN(-1);
        return;
    }

    uint8_t cable = (uint8_t)GET_INT_ARG(1);
    uint8_t cin   = (uint8_t)GET_INT_ARG(2);
    uint8_t midi1 = (uint8_t)GET_INT_ARG(3);
    uint8_t midi2 = (uint8_t)GET_INT_ARG(4);
    uint8_t midi3 = (uint8_t)GET_INT_ARG(5);

    int ret = USB_MIDI_DEVICE_send_packet(cable, cin, midi1, midi2, midi3);
    SET_INT_RETURN(ret);
}

/* USB_MIDI_DEVICE._bytes_available */
static void
c_usb_midi_device_bytes_available(mrbc_vm *vm, mrbc_value v[], int argc)
{
    int available = USB_MIDI_DEVICE_bytes_available();
    SET_INT_RETURN(available);
}

/* USB_MIDI_DEVICE._read_available */
static void
c_usb_midi_device_read_available(mrbc_vm *vm, mrbc_value v[], int argc)
{
    int available = USB_MIDI_DEVICE_bytes_available();

    if (available < 4) {
        SET_NIL_RETURN();
        return;
    }

    uint8_t buffer[64];
    size_t max_read = (available > 64) ? 64 : available;

    int read_len = USB_MIDI_DEVICE_read_packet(buffer, max_read);

    if (read_len <= 0) {
        SET_NIL_RETURN();
        return;
    }

    mrbc_value str = mrbc_string_new(vm, (const char *)buffer, read_len);
    SET_RETURN(str);
}

void
mrbc_usb_midi_device_init(mrbc_vm *vm)
{
    mrbc_class *class_USB_MIDI_DEVICE = mrbc_define_class(vm, "USB_MIDI_DEVICE", mrbc_class_object);

    /* NOTE: No CIN_* class constants here (unlike an earlier revision).
     * They were never referenced from Ruby (picoruby-midi has its own CIN
     * constants), and registering class constants via mrbc_set_class_const
     * during picogem require was the prime suspect in a VM heap corruption
     * crash (mrbc_find_method walking a smashed Array method chain right
     * after this gem's require). Keep this init minimal, mirroring the
     * proven picoruby-usb_midi_host pattern. */

    /* Methods */
    mrbc_define_method(vm, class_USB_MIDI_DEVICE, "_init",             c_usb_midi_device_init);
    mrbc_define_method(vm, class_USB_MIDI_DEVICE, "_connected",        c_usb_midi_device_connected);
    mrbc_define_method(vm, class_USB_MIDI_DEVICE, "_send_packet",      c_usb_midi_device_send_packet);
    mrbc_define_method(vm, class_USB_MIDI_DEVICE, "_bytes_available",  c_usb_midi_device_bytes_available);
    mrbc_define_method(vm, class_USB_MIDI_DEVICE, "_read_available",   c_usb_midi_device_read_available);
}
