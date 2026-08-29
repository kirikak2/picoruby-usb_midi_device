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
#if USB_MIDI_DEVICE_WITH_CDC
/* IAD is required for composite devices with CDC */
#define USB_MIDI_DEVICE_DESC_CLASS     TUSB_CLASS_MISC
#define USB_MIDI_DEVICE_DESC_SUBCLASS  MISC_SUBCLASS_COMMON
#define USB_MIDI_DEVICE_DESC_PROTOCOL  MISC_PROTOCOL_IAD
#else
/* MIDI-only: class information lives in the interface descriptors */
#define USB_MIDI_DEVICE_DESC_CLASS     TUSB_CLASS_UNSPECIFIED
#define USB_MIDI_DEVICE_DESC_SUBCLASS  0x00
#define USB_MIDI_DEVICE_DESC_PROTOCOL  0x00
#endif

const tusb_desc_device_t usb_midi_device_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_MIDI_DEVICE_BCD_USB,
    .bDeviceClass       = USB_MIDI_DEVICE_DESC_CLASS,
    .bDeviceSubClass    = USB_MIDI_DEVICE_DESC_SUBCLASS,
    .bDeviceProtocol    = USB_MIDI_DEVICE_DESC_PROTOCOL,
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

/*
 * The two configurations differ only in bulk endpoint size, which the USB
 * spec fixes per bus speed: 64 bytes at full speed, 512 at high speed. The
 * interrupt notification endpoint is unaffected.
 *
 * A high-speed-capable device must publish both, because the host asks for
 * the other speed's configuration (GET_DESCRIPTOR OTHER_SPEED_CONFIGURATION)
 * to learn what it would get if it re-enumerated at the other speed.
 */
#define USB_MIDI_DEVICE_CONFIG_DESCRIPTOR(epsize)                       \
    /* Configuration header: bus-powered, 100 mA */                     \
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100), \
    CDC_DESCRIPTOR_OR_NOTHING(epsize)                                   \
    /* MIDI Audio Control + Streaming interfaces */                     \
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, STRID_MIDI,                       \
                        USB_MIDI_DEVICE_EPNUM_MIDI_OUT,                 \
                        USB_MIDI_DEVICE_EPNUM_MIDI_IN, epsize)

#if USB_MIDI_DEVICE_WITH_CDC
#define CDC_DESCRIPTOR_OR_NOTHING(epsize)                               \
    /* CDC: notif EP, then a bulk data EP pair */                       \
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC,                          \
                       USB_MIDI_DEVICE_EPNUM_CDC_NOTIF, 8,              \
                       USB_MIDI_DEVICE_EPNUM_CDC_OUT,                   \
                       USB_MIDI_DEVICE_EPNUM_CDC_IN, epsize),
#else
#define CDC_DESCRIPTOR_OR_NOTHING(epsize)
#endif

const uint8_t usb_midi_device_desc_fs_config[] =
{
    USB_MIDI_DEVICE_CONFIG_DESCRIPTOR(64),
};

#if USB_MIDI_DEVICE_HIGH_SPEED
const uint8_t usb_midi_device_desc_hs_config[] =
{
    USB_MIDI_DEVICE_CONFIG_DESCRIPTOR(512),
};

/*
 * Device qualifier: what this device would look like at the other speed.
 * Everything matches the device descriptor except that it carries no
 * identity - it exists purely to say "I am also capable of the other speed".
 */
const tusb_desc_device_qualifier_t usb_midi_device_desc_qualifier = {
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = USB_MIDI_DEVICE_BCD_USB,
    .bDeviceClass       = USB_MIDI_DEVICE_DESC_CLASS,
    .bDeviceSubClass    = USB_MIDI_DEVICE_DESC_SUBCLASS,
    .bDeviceProtocol    = USB_MIDI_DEVICE_DESC_PROTOCOL,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0x00,
};
#endif /* USB_MIDI_DEVICE_HIGH_SPEED */

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
