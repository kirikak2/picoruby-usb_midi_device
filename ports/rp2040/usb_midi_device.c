/*
 * PicoRuby USB-MIDI Device - RP2040 / RP2350 port (R2P2)
 *
 * Unlike the ESP32 port, this gem does not run the USB stack here:
 * picoruby-machine initialises TinyUSB at boot and is the single caller of
 * tud_task() (its USB pump). The application's tusb_config.h must turn
 * CFG_TUD_MIDI on. The descriptors are either
 *
 *   - this gem's (usb_descriptors.c, USB_MIDI_DEVICE_OWN_DESCRIPTORS=1, the
 *     default): the device is the MIDI interface (plus CDC if configured),
 *     with the identity in usb_midi_device_config.h; or
 *   - the application's (USB_MIDI_DEVICE_OWN_DESCRIPTORS=0): it puts a MIDI
 *     interface among its own, as R2P2 does in its composite device (CDC x3,
 *     HID x3) when built with R2P2_USB_MIDI=1.
 *
 * Either way this file is the same. TinyUSB has no locking (OPT_OS_NONE),
 * so every TinyUSB call of this port runs inside tud_task(): in
 * tud_midi_rx_cb(), or in a function the VM queues there with TinyUSB's own
 * usbd_defer_func():
 *
 *   TX: VM -> tx ring -> (deferred) tud_midi_packet_write()
 *   RX: tud_midi_rx_cb() -> tud_midi_packet_read() -> rx ring -> VM
 *
 * usbd_defer_func() puts the call in the queue tud_task() empties, with the
 * USB interrupt off, so it is safe from thread context; the pump runs at
 * least every 1 ms (picoruby-machine's tick). Call this port from thread
 * context only: deferring from inside tud_task() would keep tud_task()
 * going round until the queue is empty.
 *
 * The rings are guarded by disabling interrupts around each push and pop.
 * A packet the rx ring has no room for stays in TinyUSB's FIFO, which then
 * NAKs the host (backpressure rather than loss), and is fetched once the
 * VM has read some.
 *
 * Without USB_MIDI_DEVICE_ENABLED (R2P2's cmake sets it with R2P2_USB_MIDI)
 * this file is the stubs.
 */

#include "../../include/usb_midi_device_config.h"
#include "../../include/usb_midi_device.h"

#include <string.h>

#if USB_MIDI_DEVICE_ENABLED

#include "pico/time.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "device/usbd_pvt.h"   /* usbd_defer_func */

#if !CFG_TUD_MIDI
#error "USB_MIDI_DEVICE_ENABLED needs CFG_TUD_MIDI (build with R2P2_USB_MIDI=1)"
#endif

/* Both rings hold whole 4-byte USB-MIDI packets. Power of 2. */
#define RX_PACKETS (USB_MIDI_DEVICE_RX_BUFFER_SIZE / 4)
#ifndef USB_MIDI_DEVICE_TX_PACKETS
#define USB_MIDI_DEVICE_TX_PACKETS 64
#endif
#define TX_PACKETS USB_MIDI_DEVICE_TX_PACKETS

typedef struct {
    uint32_t head, tail;   /* free-running */
} ring_t;

static uint8_t g_rx_data[RX_PACKETS][4];
static ring_t  g_rx;
static uint8_t g_tx_data[TX_PACKETS][4];
static ring_t  g_tx;

/* Diagnostics (USB_MIDI_DEVICE_get_stats) */
static volatile uint32_t s_midi_rx_pkts, s_midi_tx_ok, s_midi_tx_fail,
                         s_midi_tx_drop, s_tusb_task_ms;

static inline uint32_t uptime_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static bool ring_push(ring_t *r, uint8_t (*data)[4], uint32_t size, const uint8_t *packet)
{
    bool ok = false;
    uint32_t irq = save_and_disable_interrupts();
    if (r->head - r->tail < size) {
        memcpy(data[r->head % size], packet, 4);
        r->head++;
        ok = true;
    }
    restore_interrupts(irq);
    return ok;
}

static uint32_t ring_count(const ring_t *r)
{
    return r->head - r->tail;   /* one aligned read each: no lock needed */
}

/*--------------------------------------------------------------------+
 * Inside tud_task()
 *--------------------------------------------------------------------*/

/* TinyUSB still holds packets the rx ring had no room for */
static volatile bool s_rx_waiting;

/* Move what TinyUSB received into the rx ring while it has room */
static void rx_drain(void)
{
    uint8_t packet[4];
    while (ring_count(&g_rx) < RX_PACKETS && tud_midi_packet_read(packet)) {
        if (ring_push(&g_rx, g_rx_data, RX_PACKETS, packet)) {
            s_midi_rx_pkts++;
        }
    }
    s_rx_waiting = tud_midi_available() > 0;
}

/* Hand the tx ring to TinyUSB while its FIFO takes them. Only this pops,
 * so the packet at the tail stays put while it is being written. What the
 * FIFO cannot take waits for the next send. */
static void tx_drain(void)
{
    while (ring_count(&g_tx) > 0) {
        if (!tud_ready()) {
            /* Unplugged / not configured: nothing will take them */
            uint32_t irq = save_and_disable_interrupts();
            s_midi_tx_drop += g_tx.head - g_tx.tail;
            g_tx.tail = g_tx.head;
            restore_interrupts(irq);
            return;
        }
        if (!tud_midi_packet_write(g_tx_data[g_tx.tail % TX_PACKETS])) {
            s_midi_tx_fail++;
            return;
        }
        uint32_t irq = save_and_disable_interrupts();
        g_tx.tail++;
        restore_interrupts(irq);
        s_midi_tx_ok++;
    }
}

/* TinyUSB: MIDI data arrived from the host */
void tud_midi_rx_cb(uint8_t itf)
{
    (void)itf;
    s_tusb_task_ms = uptime_ms();
    rx_drain();
}

/* A call queued into tud_task() is pending (and since when). One at a
 * time: TinyUSB's event queue is 16 entries, shared with the USB
 * interrupt. A call the full queue dropped is queued again after
 * DEFER_RETRY_MS rather than leaving the flag up for good. */
#define DEFER_RETRY_MS 5
static volatile bool s_deferred;
static volatile uint32_t s_deferred_ms;

static void deferred_drain(void *param)
{
    (void)param;
    s_deferred = false;
    rx_drain();
    tx_drain();
}

/* Thread context: have the next tud_task() drain both rings */
static void request_drain(void)
{
    uint32_t now = uptime_ms();
    if (s_deferred && now - s_deferred_ms < DEFER_RETRY_MS) return;
    s_deferred = true;
    s_deferred_ms = now;
    usbd_defer_func(deferred_drain, NULL, false);
}

/*--------------------------------------------------------------------+
 * Init / Start
 *--------------------------------------------------------------------*/

int USB_MIDI_DEVICE_init(void)
{
    return 0;   /* the rings are static; the USB stack is R2P2's */
}

int USB_MIDI_DEVICE_start(void)
{
    return 0;   /* R2P2 started TinyUSB at boot, MIDI interface included */
}

int USB_MIDI_DEVICE_set_cdc_rx_callback(usb_midi_device_cdc_rx_cb_t callback,
                                        void *arg)
{
    (void)callback; (void)arg;
    return -1;   /* the CDC interfaces are R2P2's */
}

/*--------------------------------------------------------------------+
 * Status
 *--------------------------------------------------------------------*/

bool USB_MIDI_DEVICE_connected(void)
{
    return tud_ready() && tud_midi_mounted();
}

/*--------------------------------------------------------------------+
 * TX (thread context)
 *--------------------------------------------------------------------*/

int USB_MIDI_DEVICE_send_packet(uint8_t cable, uint8_t cin,
                                uint8_t midi1, uint8_t midi2, uint8_t midi3)
{
    if (!USB_MIDI_DEVICE_connected()) {
        s_midi_tx_drop++;
        return -1;
    }
    uint8_t packet[4] = {
        (uint8_t)((cable << 4) | (cin & 0x0F)), midi1, midi2, midi3
    };
    bool ok = ring_push(&g_tx, g_tx_data, TX_PACKETS, packet);
    request_drain();   /* also what an earlier full FIFO left */
    if (!ok) {
        s_midi_tx_drop++;
        return -1;
    }
    return 0;
}

/*--------------------------------------------------------------------+
 * RX: ring buffer API (thread context)
 *--------------------------------------------------------------------*/

int USB_MIDI_DEVICE_bytes_available(void)
{
    return (int)(ring_count(&g_rx) * 4);
}

int USB_MIDI_DEVICE_read_packet(uint8_t *out_buffer, size_t max_len)
{
    if (out_buffer == NULL || max_len < 4) return 0;
    size_t n = 0;
    uint32_t irq = save_and_disable_interrupts();
    while (n + 4 <= max_len && g_rx.head != g_rx.tail) {
        memcpy(out_buffer + n, g_rx_data[g_rx.tail % RX_PACKETS], 4);
        g_rx.tail++;
        n += 4;
    }
    restore_interrupts(irq);
    if (n > 0 && s_rx_waiting) request_drain();   /* room for what TinyUSB holds */
    return (int)n;
}

void USB_MIDI_DEVICE_push_rx_packet(const uint8_t *packet)
{
    if (packet == NULL) return;
    ring_push(&g_rx, g_rx_data, RX_PACKETS, packet);
}

/*--------------------------------------------------------------------+
 * Diagnostics
 *--------------------------------------------------------------------*/

void USB_MIDI_DEVICE_get_stats(usb_midi_device_stats_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    out->mounted   = tud_mounted();
    out->suspended = tud_suspended();
    out->tx_queue_waiting = ring_count(&g_tx);
    out->midi_rx_pkts = s_midi_rx_pkts;
    out->midi_tx_ok   = s_midi_tx_ok;
    out->midi_tx_fail = s_midi_tx_fail;
    out->midi_tx_drop = s_midi_tx_drop;
    out->tusb_task_ms = s_tusb_task_ms;
}

#else /* !USB_MIDI_DEVICE_ENABLED */

/*--------------------------------------------------------------------+
 * Stubs for builds without the MIDI interface
 *--------------------------------------------------------------------*/

int  USB_MIDI_DEVICE_init(void)                                          { return -1; }
int  USB_MIDI_DEVICE_start(void)                                         { return -1; }
bool USB_MIDI_DEVICE_connected(void)                                     { return false; }
int  USB_MIDI_DEVICE_send_packet(uint8_t c, uint8_t ci,
                                  uint8_t m1, uint8_t m2, uint8_t m3)   { return -1; }
int  USB_MIDI_DEVICE_bytes_available(void)                               { return 0; }
int  USB_MIDI_DEVICE_read_packet(uint8_t *b, size_t l)                   { return 0; }
void USB_MIDI_DEVICE_push_rx_packet(const uint8_t *p)                   {}
void USB_MIDI_DEVICE_get_stats(usb_midi_device_stats_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
}
int  USB_MIDI_DEVICE_set_cdc_rx_callback(usb_midi_device_cdc_rx_cb_t cb,
                                          void *arg)                     { return -1; }

#endif /* USB_MIDI_DEVICE_ENABLED */
