/*
 * PicoRuby USB-MIDI Device Driver
 *
 * USB MIDI device transport layer for PicoRuby (ESP32-P4 / ESP32-S3).
 *
 * Build-time configuration (USB identity, optional CDC interface, task
 * placement) lives in usb_midi_device_config.h.
 */

#ifndef USB_MIDI_DEVICE_DEFINED_H_
#define USB_MIDI_DEVICE_DEFINED_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RX ring buffer size (must be power of 2) */
#define USB_MIDI_DEVICE_RX_BUFFER_SIZE 512

/* SPSC ring buffer for RX (USB task writes, Ruby task reads) */
typedef struct {
    volatile uint32_t head;  /* Written by USB/TinyUSB task */
    volatile uint32_t tail;  /* Written by Ruby task */
    volatile uint8_t *data;
} usb_midi_device_rx_buffer_t;

/*
 * USB-MIDI Code Index Numbers (CIN) - same as host side
 */
#define USB_MIDI_DEVICE_CIN_MISC              0x00
#define USB_MIDI_DEVICE_CIN_CABLE_EVENT       0x01
#define USB_MIDI_DEVICE_CIN_SYSCOMMON_2       0x02
#define USB_MIDI_DEVICE_CIN_SYSCOMMON_3       0x03
#define USB_MIDI_DEVICE_CIN_SYSEX_START       0x04
#define USB_MIDI_DEVICE_CIN_SYSCOMMON_1       0x05
#define USB_MIDI_DEVICE_CIN_SYSEX_END_2       0x06
#define USB_MIDI_DEVICE_CIN_SYSEX_END_3       0x07
#define USB_MIDI_DEVICE_CIN_NOTE_OFF          0x08
#define USB_MIDI_DEVICE_CIN_NOTE_ON           0x09
#define USB_MIDI_DEVICE_CIN_POLY_KEY          0x0A
#define USB_MIDI_DEVICE_CIN_CONTROL_CHANGE    0x0B
#define USB_MIDI_DEVICE_CIN_PROGRAM_CHANGE    0x0C
#define USB_MIDI_DEVICE_CIN_CHANNEL_PRESSURE  0x0D
#define USB_MIDI_DEVICE_CIN_PITCH_BEND        0x0E
#define USB_MIDI_DEVICE_CIN_SINGLE_BYTE       0x0F

/*
 * Initialize RX ring buffer (called before start)
 */
int USB_MIDI_DEVICE_init(void);

/*
 * Install the TinyUSB driver and start the USB-MIDI device (plus a CDC
 * interface when built with USB_MIDI_DEVICE_WITH_CDC=1).
 * Returns 0 on success, -1 when the port is disabled or install failed.
 */
int USB_MIDI_DEVICE_start(void);

/*
 * CDC-ACM receive hook (only when built with USB_MIDI_DEVICE_WITH_CDC=1).
 *
 * The callback is invoked from the TinyUSB task with the bytes just read
 * from the CDC interface; it may be called several times per USB transfer
 * (at most USB_MIDI_DEVICE_CDC_RX_CHUNK bytes each). `data` is only valid
 * for the duration of the call, so copy anything you need to keep.
 *
 * Keep the callback short and non-blocking: it runs on the same task that
 * services USB, so stalling it stalls MIDI traffic as well.
 *
 * The callback is deliberately NOT defined by this gem — the host
 * application supplies it (console command parser, protocol handler, ...).
 * Registration works before or after USB_MIDI_DEVICE_start(); pass NULL to
 * unregister.
 *
 * Returns 0 on success, -1 when this build has no CDC interface.
 */
typedef void (*usb_midi_device_cdc_rx_cb_t)(const uint8_t *data, size_t len,
                                            void *arg);

int USB_MIDI_DEVICE_set_cdc_rx_callback(usb_midi_device_cdc_rx_cb_t callback,
                                        void *arg);

/*
 * Returns true when the USB host has enumerated this device (ready state).
 */
bool USB_MIDI_DEVICE_connected(void);

/*
 * Send a USB-MIDI packet to the host (device → host / MIDI IN jack).
 * Returns 0 on success, -1 on error.
 */
int USB_MIDI_DEVICE_send_packet(uint8_t cable, uint8_t cin,
                                uint8_t midi1, uint8_t midi2, uint8_t midi3);

/*
 * Get number of bytes available in RX ring buffer (host → device / MIDI OUT jack).
 */
int USB_MIDI_DEVICE_bytes_available(void);

/*
 * Read up to max_len bytes from RX ring buffer into out_buffer.
 * Data is in 4-byte USB-MIDI packet format.
 * Returns number of bytes actually read.
 */
int USB_MIDI_DEVICE_read_packet(uint8_t *out_buffer, size_t max_len);

/*
 * Bridge: push one 4-byte USB-MIDI packet into the RX ring buffer.
 * Called from TinyUSB task context (tud_midi_rx_cb).
 */
void USB_MIDI_DEVICE_push_rx_packet(const uint8_t *packet);

/*
 * Diagnostics: a snapshot of the TinyUSB device state and of the traffic
 * counters kept by this gem. Safe to call from any task; the values are
 * read without locking, so a snapshot may be slightly inconsistent.
 *
 * Endpoint flags are bitmasks: bit 0 MIDI OUT, bit 1 MIDI IN, bit 2 CDC
 * OUT, bit 3 CDC IN (the last two stay 0 without a CDC interface).
 */
typedef struct {
    bool     mounted;
    bool     suspended;
    bool     cdc_connected;     /* DTR set by the host */
    uint8_t  ep_busy;
    uint8_t  ep_stalled;
    uint32_t cdc_rx_avail;      /* bytes waiting in TinyUSB's CDC RX FIFO */
    uint32_t cdc_tx_free;       /* free space in TinyUSB's CDC TX FIFO */
    uint32_t midi_rx_avail;     /* bytes waiting in TinyUSB's MIDI RX FIFO */
    uint32_t tx_queue_waiting;  /* packets queued for the TX task */

    /* DCD events seen by the usbd queue (counted when queued, from ISR) */
    uint32_t ev_total;
    uint32_t ev_bus_reset;
    uint32_t ev_suspend;
    uint32_t ev_resume;
    uint32_t ev_setup;
    uint32_t ev_xfer;
    uint32_t ev_last_ms;        /* uptime of the last event */

    uint32_t midi_rx_pkts;      /* host -> device */
    uint32_t midi_tx_ok;        /* device -> host, accepted by TinyUSB */
    uint32_t midi_tx_fail;      /* tud_midi_packet_write() returned 0 */
    uint32_t midi_tx_drop;      /* not ready / TX queue full */
    uint32_t cdc_rx_bytes;
    uint32_t cdc_rx_last_ms;
    uint32_t tusb_task_ms;      /* last time the TinyUSB task ran a callback */
} usb_midi_device_stats_t;

void USB_MIDI_DEVICE_get_stats(usb_midi_device_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* USB_MIDI_DEVICE_DEFINED_H_ */
