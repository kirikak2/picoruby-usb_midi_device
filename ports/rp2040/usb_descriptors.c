/*
 * USB descriptors for the PicoRuby USB-MIDI device (RP2040 / RP2350).
 *
 * For a device whose USB is this gem's: MIDI-only by default, and building
 * with USB_MIDI_DEVICE_WITH_CDC=1 adds a CDC-ACM interface (composite
 * device, IAD device class). Identity strings, VID/PID and endpoint numbers
 * all come from include/usb_midi_device_config.h, which a host project
 * overrides with -D flags -- nothing product-specific lives in this file.
 * The tables are the ESP32 port's (ports/esp32/usb_descriptors.c), less the
 * high-speed ones: the RP2040 / RP2350 USB is full speed only.
 *
 * pico-sdk's TinyUSB asks for descriptors through the tud_descriptor_*_cb()
 * callbacks, so they are defined here (on ESP32 the tables go to
 * tinyusb_driver_install() instead). An application that has descriptors of
 * its own (R2P2, which adds a MIDI interface to its composite device) builds
 * with USB_MIDI_DEVICE_OWN_DESCRIPTORS=0, and this file is empty; defining
 * the callbacks twice would not link.
 */

#include "../../include/usb_midi_device_config.h"

#if USB_MIDI_DEVICE_ENABLED && USB_MIDI_DEVICE_OWN_DESCRIPTORS

#include "tusb.h"
#include "usb_descriptors.h"

#if !CFG_TUD_MIDI
#error "picoruby-usb_midi_device needs a MIDI interface (CFG_TUD_MIDI in tusb_config.h)"
#endif

#if USB_MIDI_DEVICE_WITH_CDC && !CFG_TUD_CDC
#error "USB_MIDI_DEVICE_WITH_CDC=1 needs CFG_TUD_CDC in tusb_config.h"
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

const uint8_t usb_midi_device_desc_fs_config[] =
{
    /* Configuration header: bus-powered, 100 mA */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
#if USB_MIDI_DEVICE_WITH_CDC
    /* CDC: notif EP, then a bulk data EP pair */
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
 * String Descriptors: [0] = language code, then indexed by
 * iManufacturer / iProduct / iSerialNumber / interface strings
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

/*--------------------------------------------------------------------+
 * TinyUSB callbacks
 *--------------------------------------------------------------------*/

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&usb_midi_device_desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;   /* one configuration */
    return usb_midi_device_desc_fs_config;
}

/* The ASCII strings above as UTF-16LE string descriptors. TinyUSB sends the
 * buffer after the callback returns, so it is static (one request at a time
 * on the control endpoint). */
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t desc[32 + 1];
    uint8_t count;

    if (index == 0) {
        desc[1] = (uint16_t)(USB_MIDI_DEVICE_LANGID_LO | (USB_MIDI_DEVICE_LANGID_HI << 8));
        count = 1;
    } else {
        if (index >= usb_midi_device_string_desc_count) return NULL;
        const char *str = usb_midi_device_string_desc[index];
        count = 0;
        while (str[count] && count < 32) {
            desc[1 + count] = (uint8_t)str[count];
            count++;
        }
    }
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return desc;
}

#endif /* USB_MIDI_DEVICE_ENABLED && USB_MIDI_DEVICE_OWN_DESCRIPTORS */
