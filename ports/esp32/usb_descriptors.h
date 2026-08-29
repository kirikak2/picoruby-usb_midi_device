/*
 * Descriptor tables exported by usb_descriptors.c.
 *
 * Internal to the ESP32 port: usb_midi_device.c passes these to
 * tinyusb_driver_install(). Their content is driven entirely by
 * include/usb_midi_device_config.h.
 */

#ifndef PICORUBY_USB_MIDI_DEVICE_DESCRIPTORS_H_
#define PICORUBY_USB_MIDI_DEVICE_DESCRIPTORS_H_

#include "tusb.h"
#include "../../include/usb_midi_device_config.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const tusb_desc_device_t usb_midi_device_desc_device;
extern const uint8_t            usb_midi_device_desc_fs_config[];
extern const char              *usb_midi_device_string_desc[];
extern const int                usb_midi_device_string_desc_count;

#if USB_MIDI_DEVICE_HIGH_SPEED
/* Only built for the high-speed port; the full-speed one never asks. */
extern const uint8_t                       usb_midi_device_desc_hs_config[];
extern const tusb_desc_device_qualifier_t  usb_midi_device_desc_qualifier;
#endif

#ifdef __cplusplus
}
#endif

#endif /* PICORUBY_USB_MIDI_DEVICE_DESCRIPTORS_H_ */
