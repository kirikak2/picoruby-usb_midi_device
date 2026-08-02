/*
 * PicoRuby USB-MIDI Device - ESP32 port (ESP32-P4 / ESP32-S3)
 *
 * Uses TinyUSB (via esp_tinyusb managed component) to expose a USB-MIDI
 * device — optionally composite with CDC-ACM, see USB_MIDI_DEVICE_WITH_CDC
 * — on the full-speed OTG port:
 *   ESP32-P4 (e.g. M5Stack Tab5): USB-C (USB1), USB-A stays a USB host
 *   ESP32-S3: the single USB-C connector; the USB-MIDI host role is
 *     unavailable in this mode (one PHY, one controller)
 *
 * Thread model:
 *   TX: any task/timer → queue → TX task (same core as tud_task)
 *                              → tud_midi_packet_write()
 *   RX: TinyUSB task   → tud_midi_rx_cb() → SPSC ring buffer
 *       Ruby task      → USB_MIDI_DEVICE_read_packet() ← ring buffer
 */

#include "../../include/usb_midi_device_config.h"
#include "../../include/usb_midi_device.h"

#if USB_MIDI_DEVICE_ENABLED

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#if CONFIG_IDF_TARGET_ESP32P4 && USB_MIDI_DEVICE_P4_PHY_SWAP
#include "hal/usb_serial_jtag_ll.h"
#endif
#include "esp_cpu.h"
#include "esp_ipc.h"
#include "tinyusb.h"
#if USB_MIDI_DEVICE_WITH_CDC
#include "tinyusb_cdc_acm.h"
#if USB_MIDI_DEVICE_CDC_CONSOLE
#include "tinyusb_console.h"
#endif
#endif
#include "tusb.h"
#include "usb_descriptors.h"

static const char *TAG = "USB_MIDI_DEV";

#define RX_BUFFER_MASK (USB_MIDI_DEVICE_RX_BUFFER_SIZE - 1)

static usb_midi_device_rx_buffer_t g_rx_buffer;

/*
 * MIDI TX must always run on the SAME core as the TinyUSB device task
 * (tud_task, pinned to Core 1). tud_midi_packet_write() reaches into the
 * shared usbd/FIFO layer (tu_fifo_*, usbd_edpt_claim/xfer) which is only
 * safe against tud_task via same-core FreeRTOS preemption + the atomic
 * endpoint claim. It is NOT safe to run in TRUE parallel with tud_task.
 *
 * MIDI TX is requested from multiple task contexts and cores:
 *   - the Ruby VM task            (Core 1) — note_on / send_clock
 *   - the esp_timer callbacks     (Core 0) — note-scheduler auto note_off,
 *                                            MIDI clock tick
 * A Core-0 caller invoking tud_midi_packet_write() runs in true parallel
 * with tud_task on Core 1 and corrupts usbd/FIFO state, which manifests as
 * wild DRAM writes crashing the mruby/c VM (e.g. a stray value clobbering
 * mrbc_class_Array.method_link).
 *
 * Fix: funnel EVERY send through a FreeRTOS queue drained by a single task
 * pinned to Core 1. tud_midi_packet_write() is then only ever called from
 * Core 1, serialized with tud_task by same-core scheduling regardless of
 * which core/context requested the send.
 */
static QueueHandle_t g_tx_queue = NULL;
static TaskHandle_t  g_tx_task  = NULL;

/* Core-1 TX task: the sole caller of tud_midi_packet_write(). */
static void usb_midi_tx_task(void *arg)
{
    (void)arg;
    uint8_t packet[4];
    for (;;) {
        if (xQueueReceive(g_tx_queue, packet, portMAX_DELAY) == pdTRUE) {
            if (tud_ready()) {
                tud_midi_packet_write(packet);
            }
        }
    }
}

/*--------------------------------------------------------------------+
 * CDC-ACM receive hook (optional interface)
 *
 * The gem owns the esp_tinyusb registration; the actual handler is
 * supplied by the application through USB_MIDI_DEVICE_set_cdc_rx_callback().
 *--------------------------------------------------------------------*/

#if USB_MIDI_DEVICE_WITH_CDC
static usb_midi_device_cdc_rx_cb_t g_cdc_rx_cb  = NULL;
static void                       *g_cdc_rx_arg = NULL;

/* Runs in TinyUSB task context. Drains the CDC FIFO into the callback. */
static void usb_midi_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)event;
    uint8_t buf[USB_MIDI_DEVICE_CDC_RX_CHUNK];

    for (;;) {
        size_t rx = 0;
        esp_err_t err = tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)itf,
                                            buf, sizeof(buf), &rx);
        if (err != ESP_OK || rx == 0) break;

        usb_midi_device_cdc_rx_cb_t cb = g_cdc_rx_cb;
        if (cb) cb(buf, rx, g_cdc_rx_arg);
    }
}
#endif

int USB_MIDI_DEVICE_set_cdc_rx_callback(usb_midi_device_cdc_rx_cb_t callback,
                                        void *arg)
{
#if USB_MIDI_DEVICE_WITH_CDC
    g_cdc_rx_arg = arg;
    g_cdc_rx_cb  = callback;   /* set the arg first: the TinyUSB task may
                                * call the callback as soon as it is stored */
    return 0;
#else
    (void)callback; (void)arg;
    ESP_LOGW(TAG, "CDC RX callback ignored: built without USB_MIDI_DEVICE_WITH_CDC");
    return -1;
#endif
}

/*--------------------------------------------------------------------+
 * Init / Start
 *--------------------------------------------------------------------*/

int USB_MIDI_DEVICE_init(void)
{
    if (g_tx_queue == NULL) {
        g_tx_queue = xQueueCreate(USB_MIDI_DEVICE_TX_QUEUE_DEPTH, 4 /* bytes/packet */);
        if (g_tx_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create TX queue");
            return -1;
        }
    }
    if (g_rx_buffer.data == NULL) {
        /* Heap-allocate to avoid perturbing .bss layout (see CLAUDE.md) */
        g_rx_buffer.data = (volatile uint8_t *)heap_caps_malloc(
            USB_MIDI_DEVICE_RX_BUFFER_SIZE,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (g_rx_buffer.data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate RX buffer (%d bytes)",
                     USB_MIDI_DEVICE_RX_BUFFER_SIZE);
            return -1;
        }
    }
    g_rx_buffer.head = 0;
    g_rx_buffer.tail = 0;
    ESP_LOGI(TAG, "USB MIDI Device RX buffer initialized");
    return 0;
}

int USB_MIDI_DEVICE_start(void)
{
    int ret = USB_MIDI_DEVICE_init();
    if (ret != 0) return ret;

#if CONFIG_IDF_TARGET_ESP32P4 && USB_MIDI_DEVICE_P4_PHY_SWAP
    /*
     * ESP32-P4 has two internal FSLS PHYs behind a mux (LP_SYS.usb_ctrl):
     *   default: USJ → PHY 0, USB OTG1.1 → PHY 1
     * On boards like the Tab5 the USB-C connector is wired to PHY 0's pads
     * (that is why USJ flashing/monitor works there). Swap the mux so
     * OTG1.1 gets PHY 0 and USJ is parked on the unrouted PHY 1.
     * Set USB_MIDI_DEVICE_P4_PHY_SWAP=0 on a board wired the other way.
     *
     * Neither ESP-IDF's usb_phy driver nor esp_tinyusb performs this
     * routing, so we must do it ourselves. We use the USJ LL helper
     * (arg = PHY index for USJ): 1 → USJ:PHY1 / USB_WRAP:PHY0.
     * (usb_wrap_ll_phy_select() has a fallthrough bug; avoid it.)
     *
     * NOTE: After this point USB-Serial-JTAG is disconnected from USB-C.
     */
    usb_serial_jtag_ll_phy_select(1);
#else
    /*
     * ESP32-S3 has a single internal FSLS PHY shared by USB-Serial/JTAG and
     * USB-OTG; ESP-IDF's usb_phy driver (called by tinyusb_driver_install
     * with phy.skip_setup = false) hands it to USB-OTG for us, so there is
     * no mux to program here.
     *
     * NOTE: as on P4, USB-Serial/JTAG is disconnected from the connector
     * afterwards — flashing needs manual download mode (BOOT + RESET).
     */
#endif

    /*
     * Install TinyUSB on the full-speed OTG port (TINYUSB_PORT_FULL_SPEED_0):
     * on P4 that is USB1 = USB-C, leaving USB0 (HS OTG = USB-A) to the USB
     * host driver for MIDI keyboards; on S3 it is the only OTG port.
     *
     * task config is mandatory: tinyusb_task_check_config() rejects
     * size==0 / priority==0, so a partially zeroed struct fails install.
     *
     * Descriptors MUST be passed through the config struct: esp_tinyusb
     * defines tud_descriptor_*_cb() itself, so callback-style descriptors
     * would be ignored and the default CDC-only descriptor used instead.
     */
    const tinyusb_config_t tusb_cfg = {
        .port = TINYUSB_PORT_FULL_SPEED_0,
        .phy = {
            .skip_setup = false,
            .self_powered = false,
            .vbus_monitor_io = -1,
        },
        .task = {
            .size = USB_MIDI_DEVICE_TUSB_TASK_STACK_SIZE,
            .priority = USB_MIDI_DEVICE_TUSB_TASK_PRIORITY,
            /* Pin to the same core as the TX task below (and, in a PicoRuby
             * build, as the VM task). MIDI TX reaches into the shared usbd
             * layer (usbd_edpt_claim/xfer); if the TinyUSB device task runs
             * on a different core those calls execute in TRUE parallel with
             * tud_task and corrupt usbd/FIFO state (observed: flaky VM-heap
             * corruption crashing mruby/c). Same-core placement lets
             * FreeRTOS preemption + the atomic endpoint claim serialize them
             * safely. */
            .xCoreID = USB_MIDI_DEVICE_TASK_CORE,
        },
        .descriptor = {
            .device = &usb_midi_device_desc_device,
            .qualifier = NULL,
            .string = usb_midi_device_string_desc,
            .string_count = usb_midi_device_string_desc_count,
            .full_speed_config = usb_midi_device_desc_fs_config,
            .high_speed_config = NULL,  /* FS port only */
        },
    };

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(err));
        return -1;
    }

#if USB_MIDI_DEVICE_WITH_CDC
    /* Bring up CDC ACM. The RX callback is registered unconditionally so an
     * application handler installed later (or earlier) is honoured; the gem
     * itself only forwards the bytes. */
    const tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port                   = TINYUSB_CDC_ACM_0,
        .callback_rx                = usb_midi_cdc_rx_callback,
        .callback_rx_wanted_char    = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };

    err = tinyusb_cdcacm_init(&acm_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CDC ACM init failed: %s", esp_err_to_name(err));
    }
#if USB_MIDI_DEVICE_CDC_CONSOLE
    else {
        /* Redirect stdout / ESP_LOG to USB CDC so a serial monitor works on
         * the same connector */
        err = tinyusb_console_init(0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "tinyusb_console_init failed: %s", esp_err_to_name(err));
        }
    }
#endif
#endif /* USB_MIDI_DEVICE_WITH_CDC */

    /* Start the TX task now that TinyUSB is up. It is the only caller of
     * tud_midi_packet_write(), keeping all sends on the same core as
     * tud_task, and runs just below tud_task in priority. */
    if (g_tx_task == NULL) {
        BaseType_t tret = xTaskCreatePinnedToCore(
            usb_midi_tx_task, "usbmidi_tx",
            USB_MIDI_DEVICE_TX_TASK_STACK_SIZE, NULL,
            USB_MIDI_DEVICE_TX_TASK_PRIORITY, &g_tx_task,
            USB_MIDI_DEVICE_TASK_CORE);
        if (tret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create USB MIDI TX task");
            return -1;
        }
    }

#if USB_MIDI_DEVICE_WITH_CDC
    ESP_LOGI(TAG, "USB MIDI Device started (MIDI + CDC composite, FS OTG)");
#else
    ESP_LOGI(TAG, "USB MIDI Device started (MIDI only, FS OTG)");
#endif
    return 0;
}

/*--------------------------------------------------------------------+
 * Status
 *--------------------------------------------------------------------*/

bool USB_MIDI_DEVICE_connected(void)
{
    return tud_ready();
}

/*--------------------------------------------------------------------+
 * TX: send packet from Ruby task to host
 *--------------------------------------------------------------------*/

int USB_MIDI_DEVICE_send_packet(uint8_t cable, uint8_t cin,
                                uint8_t midi1, uint8_t midi2, uint8_t midi3)
{
    if (!tud_ready() || g_tx_queue == NULL) {
        return -1;
    }

    uint8_t packet[4];
    packet[0] = (uint8_t)((cable << 4) | (cin & 0x0F));
    packet[1] = midi1;
    packet[2] = midi2;
    packet[3] = midi3;

    /* Hand the packet to the Core-1 TX task instead of writing it here: the
     * caller may be on Core 0 (esp_timer note-scheduler / clock), and
     * tud_midi_packet_write() must not run in parallel with tud_task (Core 1).
     * Short bounded wait so a momentary full queue applies backpressure
     * rather than dropping notes; give up (drop) if it stays full. */
    if (xQueueSend(g_tx_queue, packet, pdMS_TO_TICKS(5)) != pdTRUE) {
        return -1;
    }
    return 0;
}

/*--------------------------------------------------------------------+
 * RX: ring buffer API (called from Ruby task)
 *--------------------------------------------------------------------*/

int USB_MIDI_DEVICE_bytes_available(void)
{
    uint32_t head = g_rx_buffer.head;
    uint32_t tail = g_rx_buffer.tail;
    return (int)((head - tail) & RX_BUFFER_MASK);
}

int USB_MIDI_DEVICE_read_packet(uint8_t *out_buffer, size_t max_len)
{
    if (out_buffer == NULL || max_len < 4) return 0;

    int available = USB_MIDI_DEVICE_bytes_available();
    if (available < 4) return 0;

    size_t to_read = ((size_t)available < max_len) ? (size_t)available : max_len;
    to_read = (to_read / 4) * 4;  /* align to 4-byte packets */

    uint32_t tail = g_rx_buffer.tail;
    for (size_t i = 0; i < to_read; i++) {
        out_buffer[i] = g_rx_buffer.data[(tail + i) & RX_BUFFER_MASK];
    }
    __sync_synchronize();
    g_rx_buffer.tail = (tail + (uint32_t)to_read) & RX_BUFFER_MASK;

    return (int)to_read;
}

/*--------------------------------------------------------------------+
 * Bridge: called from TinyUSB task via tud_midi_rx_cb
 *--------------------------------------------------------------------*/

void USB_MIDI_DEVICE_push_rx_packet(const uint8_t *packet)
{
    if (packet == NULL || g_rx_buffer.data == NULL) return;

    uint32_t head = g_rx_buffer.head;
    for (int i = 0; i < 4; i++) {
        g_rx_buffer.data[(head + (uint32_t)i) & RX_BUFFER_MASK] = packet[i];
    }
    __sync_synchronize();
    g_rx_buffer.head = (head + 4u) & RX_BUFFER_MASK;
}

/*--------------------------------------------------------------------+
 * TinyUSB callback: MIDI data arrived from host
 * Runs in TinyUSB task context (Core 0 or USB interrupt).
 *--------------------------------------------------------------------*/

void tud_midi_rx_cb(uint8_t itf)
{
    (void)itf;
    uint8_t packet[4];
    /* Drain all available 4-byte USB-MIDI packets into our ring buffer */
    while (tud_midi_packet_read(packet)) {
        USB_MIDI_DEVICE_push_rx_packet(packet);
    }
}

#else /* !USB_MIDI_DEVICE_ENABLED */

/*--------------------------------------------------------------------+
 * Stub implementations for builds where the USB port is not a MIDI device
 *--------------------------------------------------------------------*/

int  USB_MIDI_DEVICE_init(void)                                          { return -1; }
int  USB_MIDI_DEVICE_start(void)                                         { return -1; }
bool USB_MIDI_DEVICE_connected(void)                                     { return false; }
int  USB_MIDI_DEVICE_send_packet(uint8_t c, uint8_t ci,
                                  uint8_t m1, uint8_t m2, uint8_t m3)   { return -1; }
int  USB_MIDI_DEVICE_bytes_available(void)                               { return 0; }
int  USB_MIDI_DEVICE_read_packet(uint8_t *b, size_t l)                   { return 0; }
void USB_MIDI_DEVICE_push_rx_packet(const uint8_t *p)                   {}
int  USB_MIDI_DEVICE_set_cdc_rx_callback(usb_midi_device_cdc_rx_cb_t cb,
                                          void *arg)                     { return -1; }

#endif /* USB_MIDI_DEVICE_ENABLED */
