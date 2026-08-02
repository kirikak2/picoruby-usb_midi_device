/*
 * USB descriptors for the PicoRuby USB-MIDI device (ESP32-P4 / ESP32-S3).
 *
 * The device is MIDI-only by default; building with
 * USB_MIDI_DEVICE_WITH_CDC=1 adds a CDC-ACM interface (composite device,
 * IAD device class). Identity strings, VID/PID and endpoint numbers all
 * come from include/usb_midi_device_config.h, which a host project
 * overrides with -D flags — nothing product-specific lives in this file.
 *
 * NOTE: esp_tinyusb (>= 1.x new API) defines tud_descriptor_*_cb() itself
 * in descriptors_control.c. Do NOT define those callbacks here — they would
 * be silently ignored (or clash at link time). Instead, this file exports
 * descriptor tables that usb_midi_device.c passes to tinyusb_driver_install()
 * via tinyusb_config_t.descriptor.
 */

#include "../../include/usb_midi_device_config.h"

#if USB_MIDI_DEVICE_ENABLED

#include "tusb.h"
#include "usb_descriptors.h"

#if !CFG_TUD_MIDI
#error "picoruby-usb_midi_device needs a MIDI interface (CONFIG_TINYUSB_MIDI_COUNT >= 1)"
#endif

#if USB_MIDI_DEVICE_WITH_CDC && !CFG_TUD_CDC
#error "USB_MIDI_DEVICE_WITH_CDC=1 needs CFG_TUD_CDC (CONFIG_TINYUSB_CDC_ENABLED=y)"
#endif

/*--------------------------------------------------------------------+
 * Device Descriptor
 *--------------------------------------------------------------------*/
const tusb_desc_device_t usb_midi_device_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_MIDI_DEVICE_BCD_USB,
#if USB_MIDI_DEVICE_WITH_CDC
    /* IAD is required for composite devices with CDC */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
#else
    /* MIDI-only: class information lives in the interface descriptors */
    .bDeviceClass       = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
#endif
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_MIDI_DEVICE_VID,
    .idProduct          = USB_MIDI_DEVICE_PID,
    .bcdDevice          = USB_MIDI_DEVICE_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

/*--------------------------------------------------------------------+
 * Configuration Descriptor
 *--------------------------------------------------------------------*/
enum {
#if USB_MIDI_DEVICE_WITH_CDC
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
#else
    ITF_NUM_MIDI = 0,
#endif
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

/* String descriptor indices; 0-3 are langid / manufacturer / product / serial */
enum {
#if USB_MIDI_DEVICE_WITH_CDC
    STRID_CDC = 4,
    STRID_MIDI,
#else
    STRID_MIDI = 4,
#endif
};

#if USB_MIDI_DEVICE_WITH_CDC
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)
#else
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)
#endif

const uint8_t usb_midi_device_desc_fs_config[] =
{
    /* Configuration header: bus-powered, 100 mA */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

#if USB_MIDI_DEVICE_WITH_CDC
    /* CDC: notif EP, data EP pair, 64-byte bulk */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC,
                       USB_MIDI_DEVICE_EPNUM_CDC_NOTIF, 8,
                       USB_MIDI_DEVICE_EPNUM_CDC_OUT,
                       USB_MIDI_DEVICE_EPNUM_CDC_IN, 64),
#endif

    /* MIDI Audio Control + Streaming interfaces */
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, STRID_MIDI,
                        USB_MIDI_DEVICE_EPNUM_MIDI_OUT,
                        USB_MIDI_DEVICE_EPNUM_MIDI_IN, 64),
};

/*--------------------------------------------------------------------+
 * String Descriptors
 * Layout expected by esp_tinyusb: [0] = language code, then indexed
 * by iManufacturer / iProduct / iSerialNumber / interface strings.
 *--------------------------------------------------------------------*/
const char *usb_midi_device_string_desc[] = {
    (const char[]) { USB_MIDI_DEVICE_LANGID_LO,
                     USB_MIDI_DEVICE_LANGID_HI },  /* 0: language ID */
    USB_MIDI_DEVICE_MANUFACTURER,                  /* 1: Manufacturer */
    USB_MIDI_DEVICE_PRODUCT,                       /* 2: Product */
    USB_MIDI_DEVICE_SERIAL,                        /* 3: Serial */
#if USB_MIDI_DEVICE_WITH_CDC
    USB_MIDI_DEVICE_CDC_ITF_NAME,                  /* 4: CDC interface */
#endif
    USB_MIDI_DEVICE_MIDI_ITF_NAME,                 /* MIDI interface */
};

const int usb_midi_device_string_desc_count =
    (int)(sizeof(usb_midi_device_string_desc) / sizeof(usb_midi_device_string_desc[0]));

#endif /* USB_MIDI_DEVICE_ENABLED */
