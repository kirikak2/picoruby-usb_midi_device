/*
 * Descriptor tables of usb_descriptors.c (RP2040 / RP2350).
 *
 * Built only with USB_MIDI_DEVICE_OWN_DESCRIPTORS (the default): the device
 * then enumerates with these, and usb_descriptors.c hands them to TinyUSB
 * from its tud_descriptor_*_cb() callbacks. Their content is driven
 * entirely by include/usb_midi_device_config.h. An application with
 * descriptors of its own (R2P2) builds with USB_MIDI_DEVICE_OWN_DESCRIPTORS=0
 * and none of this exists.
 */

#ifndef PICORUBY_USB_MIDI_DEVICE_DESCRIPTORS_H_
#define PICORUBY_USB_MIDI_DEVICE_DESCRIPTORS_H_

#include "tusb.h"
#include "../../include/usb_midi_device_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if USB_MIDI_DEVICE_OWN_DESCRIPTORS
extern const tusb_desc_device_t usb_midi_device_desc_device;
extern const uint8_t            usb_midi_device_desc_fs_config[];
extern const char              *usb_midi_device_string_desc[];
extern const int                usb_midi_device_string_desc_count;
#endif

#ifdef __cplusplus
}
#endif

#endif /* PICORUBY_USB_MIDI_DEVICE_DESCRIPTORS_H_ */
