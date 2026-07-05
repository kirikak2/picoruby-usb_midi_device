/*
 * USB descriptors for ESP32-P4 (M5Stack Tab5) composite CDC + MIDI device.
 * Only compiled and active when CONFIG_USB_MIDI_BOARD_M5STACK_TAB5 is set.
 *
 * NOTE: esp_tinyusb (>= 1.x new API) defines tud_descriptor_*_cb() itself
 * in descriptors_control.c. Do NOT define those callbacks here — they would
 * be silently ignored (or clash at link time). Instead, this file exports
 * descriptor tables that usb_midi_device.c passes to tinyusb_driver_install()
 * via tinyusb_config_t.descriptor.
 */

#include "sdkconfig.h"  /* MUST come before the CONFIG_* check below */

#ifdef CONFIG_USB_MIDI_BOARD_M5STACK_TAB5

#include "tusb.h"

/*
 * Product ID encodes enabled classes so the host re-enumerates on changes.
 * Bit layout: [MIDI=3 | HID=2 | MSC=1 | CDC=0]
 */
#define _PID_MAP(itf, n)  ((CFG_TUD_##itf) << (n))
#define USB_PID  (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | \
                  _PID_MAP(HID, 2) | _PID_MAP(MIDI, 3))

#define USB_VID   0x303A  /* Espressif Systems VID */
#define USB_BCD   0x0200

/*--------------------------------------------------------------------+
 * Device Descriptor
 *--------------------------------------------------------------------*/
const tusb_desc_device_t midori_usb_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    /* IAD is required for composite devices with CDC */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

/*--------------------------------------------------------------------+
 * Configuration Descriptor
 *--------------------------------------------------------------------*/
enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

/*
 * Endpoint assignments for ESP32-P4 FS OTG (Full Speed, 12 Mbps).
 * EP0 is always control; user EPs start at 1.
 */
#define EPNUM_CDC_NOTIF  0x81   /* EP1 IN  - CDC notification */
#define EPNUM_CDC_OUT    0x02   /* EP2 OUT - CDC data out */
#define EPNUM_CDC_IN     0x82   /* EP2 IN  - CDC data in */
#define EPNUM_MIDI_OUT   0x03   /* EP3 OUT - MIDI data out (host→device) */
#define EPNUM_MIDI_IN    0x83   /* EP3 IN  - MIDI data in  (device→host) */

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

const uint8_t midori_usb_fs_config_descriptor[] =
{
    /* Configuration header */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    /* CDC: notif EP, data EP pair, 64-byte bulk */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4,
                       EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    /* MIDI Audio Control + Streaming interfaces */
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 5,
                        EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

/*--------------------------------------------------------------------+
 * String Descriptors
 * Layout expected by esp_tinyusb: [0] = language code, then indexed
 * by iManufacturer / iProduct / iSerialNumber / interface strings.
 *--------------------------------------------------------------------*/
const char *midori_usb_string_descriptors[] = {
    (const char[]) { 0x09, 0x04 },  /* 0: English (0x0409) */
    "Midori",                        /* 1: Manufacturer */
    "M5Stack Tab5 MIDI",             /* 2: Product */
    "MIDORI-TAB5-001",               /* 3: Serial */
    "Midori CDC",                    /* 4: CDC interface */
    "Midori MIDI",                   /* 5: MIDI interface */
};

const int midori_usb_string_descriptor_count =
    sizeof(midori_usb_string_descriptors) / sizeof(midori_usb_string_descriptors[0]);

#endif /* CONFIG_USB_MIDI_BOARD_M5STACK_TAB5 */
