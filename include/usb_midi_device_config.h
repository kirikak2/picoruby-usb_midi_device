/*
 * PicoRuby USB-MIDI Device - build-time configuration
 *
 * Every knob below has a neutral, standards-compliant default so the gem
 * builds and enumerates correctly without any project-specific settings.
 * A host project overrides what it needs, either with compiler defines
 *
 *     -DUSB_MIDI_DEVICE_PRODUCT='"Acme Groovebox"'
 *
 * or by pointing the gem at its own header
 *
 *     -DUSB_MIDI_DEVICE_CONFIG_HEADER='"my_usb_identity.h"'
 *
 * Nothing here may reference a particular product or Kconfig menu: the
 * only ESP-IDF symbols consulted are the gem-namespaced
 * CONFIG_USB_MIDI_DEVICE_* ones, which a project may declare in its own
 * Kconfig instead of passing -D flags.
 */

#ifndef PICORUBY_USB_MIDI_DEVICE_CONFIG_H_
#define PICORUBY_USB_MIDI_DEVICE_CONFIG_H_

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

/* Optional project-supplied header with the overrides below */
#ifdef USB_MIDI_DEVICE_CONFIG_HEADER
#include USB_MIDI_DEVICE_CONFIG_HEADER
#endif

/*--------------------------------------------------------------------+
 * Feature switches
 *--------------------------------------------------------------------*/

/*
 * Master switch. When 0 the port compiles to stubs that link but do
 * nothing (USB_MIDI_DEVICE_start() returns -1, connected? is false), so a
 * project can keep the gem in the build for every USB mode it supports.
 */
#ifndef USB_MIDI_DEVICE_ENABLED
#  ifdef CONFIG_USB_MIDI_DEVICE_ENABLED
#    define USB_MIDI_DEVICE_ENABLED 1
#  else
#    define USB_MIDI_DEVICE_ENABLED 0
#  endif
#endif

/*
 * Add a CDC-ACM interface next to the MIDI interface (composite device).
 * Off by default: a USB-MIDI device gem describes a MIDI device, and the
 * extra interface changes the descriptor layout, the device class (IAD)
 * and the PID. Requires CONFIG_TINYUSB_CDC_ENABLED on ESP-IDF.
 */
#ifndef USB_MIDI_DEVICE_WITH_CDC
#  ifdef CONFIG_USB_MIDI_DEVICE_WITH_CDC
#    define USB_MIDI_DEVICE_WITH_CDC 1
#  else
#    define USB_MIDI_DEVICE_WITH_CDC 0
#  endif
#endif

/*
 * Redirect stdout / ESP_LOG to the CDC interface (tinyusb_console_init).
 * Only meaningful together with USB_MIDI_DEVICE_WITH_CDC; set to 0 to keep
 * the CDC interface purely as a data pipe for the project's own protocol.
 */
#ifndef USB_MIDI_DEVICE_CDC_CONSOLE
#  define USB_MIDI_DEVICE_CDC_CONSOLE USB_MIDI_DEVICE_WITH_CDC
#endif

/*
 * Attach to the chip's high-speed (USB 2.0 OTG) port instead of the
 * full-speed (OTG 1.1) one. Only meaningful on chips that have both -- the
 * ESP32-P4 does; the ESP32-S3 has a single full-speed port and ignores this.
 *
 * Which one to pick is a board wiring fact: the device connector is soldered
 * to one port's pads and nothing else reaches it. Getting it wrong is silent
 * -- TinyUSB installs happily and drives pins that go nowhere, so the host
 * never sees a device at all.
 */
#ifndef USB_MIDI_DEVICE_HIGH_SPEED
#  ifdef CONFIG_USB_MIDI_DEVICE_HIGH_SPEED
#    define USB_MIDI_DEVICE_HIGH_SPEED 1
#  else
#    define USB_MIDI_DEVICE_HIGH_SPEED 0
#  endif
#endif

/* Bytes handed to the CDC RX callback per invocation */
#ifndef USB_MIDI_DEVICE_CDC_RX_CHUNK
#  define USB_MIDI_DEVICE_CDC_RX_CHUNK 64
#endif

/*--------------------------------------------------------------------+
 * USB identity
 *--------------------------------------------------------------------*/

/* 0x303A is Espressif's VID; products shipping in volume need their own. */
#ifndef USB_MIDI_DEVICE_VID
#  define USB_MIDI_DEVICE_VID 0x303A
#endif

/*
 * PID encodes the enabled classes so a host re-enumerates when the
 * descriptor layout changes: bit0 = CDC, bit3 = MIDI.
 */
#ifndef USB_MIDI_DEVICE_PID
#  if USB_MIDI_DEVICE_WITH_CDC
#    define USB_MIDI_DEVICE_PID 0x4009
#  else
#    define USB_MIDI_DEVICE_PID 0x4008
#  endif
#endif

#ifndef USB_MIDI_DEVICE_BCD_USB
#  define USB_MIDI_DEVICE_BCD_USB 0x0200
#endif

#ifndef USB_MIDI_DEVICE_BCD_DEVICE
#  define USB_MIDI_DEVICE_BCD_DEVICE 0x0100
#endif

#ifndef USB_MIDI_DEVICE_MANUFACTURER
#  define USB_MIDI_DEVICE_MANUFACTURER "PicoRuby"
#endif

#ifndef USB_MIDI_DEVICE_PRODUCT
#  define USB_MIDI_DEVICE_PRODUCT "PicoRuby MIDI"
#endif

#ifndef USB_MIDI_DEVICE_SERIAL
#  define USB_MIDI_DEVICE_SERIAL "000000000001"
#endif

#ifndef USB_MIDI_DEVICE_MIDI_ITF_NAME
#  define USB_MIDI_DEVICE_MIDI_ITF_NAME USB_MIDI_DEVICE_PRODUCT
#endif

#ifndef USB_MIDI_DEVICE_CDC_ITF_NAME
#  define USB_MIDI_DEVICE_CDC_ITF_NAME USB_MIDI_DEVICE_PRODUCT " CDC"
#endif

/* String descriptor 0: language ID (0x0409 = English/US), low byte first */
#ifndef USB_MIDI_DEVICE_LANGID_LO
#  define USB_MIDI_DEVICE_LANGID_LO 0x09
#endif
#ifndef USB_MIDI_DEVICE_LANGID_HI
#  define USB_MIDI_DEVICE_LANGID_HI 0x04
#endif

/*--------------------------------------------------------------------+
 * Endpoint numbers (FS OTG; EP0 is control)
 *--------------------------------------------------------------------*/

#if USB_MIDI_DEVICE_WITH_CDC
#  ifndef USB_MIDI_DEVICE_EPNUM_CDC_NOTIF
#    define USB_MIDI_DEVICE_EPNUM_CDC_NOTIF 0x81
#  endif
#  ifndef USB_MIDI_DEVICE_EPNUM_CDC_OUT
#    define USB_MIDI_DEVICE_EPNUM_CDC_OUT   0x02
#  endif
#  ifndef USB_MIDI_DEVICE_EPNUM_CDC_IN
#    define USB_MIDI_DEVICE_EPNUM_CDC_IN    0x82
#  endif
#  ifndef USB_MIDI_DEVICE_EPNUM_MIDI_OUT
#    define USB_MIDI_DEVICE_EPNUM_MIDI_OUT  0x03
#  endif
#  ifndef USB_MIDI_DEVICE_EPNUM_MIDI_IN
#    define USB_MIDI_DEVICE_EPNUM_MIDI_IN   0x83
#  endif
#else
#  ifndef USB_MIDI_DEVICE_EPNUM_MIDI_OUT
#    define USB_MIDI_DEVICE_EPNUM_MIDI_OUT  0x01
#  endif
#  ifndef USB_MIDI_DEVICE_EPNUM_MIDI_IN
#    define USB_MIDI_DEVICE_EPNUM_MIDI_IN   0x81
#  endif
#endif

/*--------------------------------------------------------------------+
 * Task placement (ESP32 port)
 *
 * tud_midi_packet_write() must never run in TRUE parallel with tud_task(),
 * so the TX task and the TinyUSB device task default to the same core.
 * Projects that pin their VM task elsewhere may move both together.
 *--------------------------------------------------------------------*/

#ifndef USB_MIDI_DEVICE_TUSB_TASK_STACK_SIZE
#  define USB_MIDI_DEVICE_TUSB_TASK_STACK_SIZE 4096
#endif
#ifndef USB_MIDI_DEVICE_TUSB_TASK_PRIORITY
#  define USB_MIDI_DEVICE_TUSB_TASK_PRIORITY 5
#endif
#ifndef USB_MIDI_DEVICE_TASK_CORE
#  define USB_MIDI_DEVICE_TASK_CORE 1
#endif
#ifndef USB_MIDI_DEVICE_TX_TASK_STACK_SIZE
#  define USB_MIDI_DEVICE_TX_TASK_STACK_SIZE 3072
#endif
#ifndef USB_MIDI_DEVICE_TX_TASK_PRIORITY
#  define USB_MIDI_DEVICE_TX_TASK_PRIORITY 4
#endif
#ifndef USB_MIDI_DEVICE_TX_QUEUE_DEPTH
#  define USB_MIDI_DEVICE_TX_QUEUE_DEPTH 64
#endif

/*
 * ESP32-P4 only: swap the FSLS PHY mux so USB-OTG1.1 drives the pads that
 * USB-Serial/JTAG owns by default. Correct for boards whose device-role
 * connector is wired to PHY 0 (e.g. M5Stack Tab5). Set to 0 on a board
 * that routes OTG1.1 to its own pads.
 *
 * The mux only feeds the two full-speed PHYs, so it is meaningless on the
 * high-speed port (which has its own UTMI PHY) and defaults off there --
 * performing it anyway would disconnect USB-Serial/JTAG for nothing.
 */
#ifndef USB_MIDI_DEVICE_P4_PHY_SWAP
#  if USB_MIDI_DEVICE_HIGH_SPEED
#    define USB_MIDI_DEVICE_P4_PHY_SWAP 0
#  else
#    define USB_MIDI_DEVICE_P4_PHY_SWAP 1
#  endif
#endif

#endif /* PICORUBY_USB_MIDI_DEVICE_CONFIG_H_ */
