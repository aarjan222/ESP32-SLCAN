#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

#define TAG "SLCAN"

// CAN pins for ESP32
#define CAN_TX_GPIO 21
#define CAN_RX_GPIO 22

// SLCAN states
static bool slcan_opened = false;
static bool slcan_listen_only = false;
static bool slcan_timestamp = false;

// Current timing configuration
static twai_timing_config_t current_timing;

// CDC buffer
#define CDC_RX_BUF_SIZE 256
#define CDC_TX_BUF_SIZE 256

static uint8_t cdc_rx_buf[CDC_RX_BUF_SIZE];
static uint8_t cdc_tx_buf[CDC_TX_BUF_SIZE];

// Command buffer
static char cmd_buffer[128];
static int cmd_index = 0;

// Get timing config by bitrate code
static bool get_timing_config(char code, twai_timing_config_t *config)
{
    twai_timing_config_t temp_config;

    switch (code)
    {
    case '0': // 10 kbit/s
        temp_config = (twai_timing_config_t){
            .clk_src = TWAI_CLK_SRC_DEFAULT,
            .quanta_resolution_hz = 1000000,
            .brp = 0,
            .tseg_1 = 63,
            .tseg_2 = 16,
            .sjw = 16,
            .triple_sampling = false};
        break;
    case '1': // 20 kbit/s
        temp_config = (twai_timing_config_t){
            .clk_src = TWAI_CLK_SRC_DEFAULT,
            .quanta_resolution_hz = 2000000,
            .brp = 0,
            .tseg_1 = 63,
            .tseg_2 = 16,
            .sjw = 16,
            .triple_sampling = false};
        break;
    case '2': // 50 kbit/s
        temp_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS();
        break;
    case '3': // 100 kbit/s
        temp_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS();
        break;
    case '4': // 125 kbit/s
        temp_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
        break;
    case '5': // 250 kbit/s
        temp_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
        break;
    case '6': // 500 kbit/s
        temp_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
        break;
    case '7': // 800 kbit/s
        temp_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS();
        break;
    case '8': // 1 Mbit/s
        temp_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
        break;
    default:
        ESP_LOGW(TAG, "Invalid bitrate code: %c", code);
        return false;
    }

    *config = temp_config;
    return true;
}

// Helper functions
static uint8_t hex_to_byte(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

static char byte_to_hex(uint8_t b)
{
    return b < 10 ? '0' + b : 'A' + b - 10;
}

// Send response via USB CDC
static void send_response(const char *resp)
{
    size_t len = strlen(resp);
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t *)resp, len);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
}

// Parse and execute SLCAN command
static void process_slcan_command(const char *cmd, size_t len)
{
    if (len < 1)
        return;

    ESP_LOGD(TAG, "Command: %c (len=%d)", cmd[0], len);

    switch (cmd[0])
    {
    case 'S': // Set bitrate
        if (len >= 2 && !slcan_opened)
        {
            if (get_timing_config(cmd[1], &current_timing))
            {
                ESP_LOGI(TAG, "Bitrate set to code %c", cmd[1]);
                send_response("\r");
            }
            else
            {
                ESP_LOGW(TAG, "Invalid bitrate code");
                send_response("\x07");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Cannot set bitrate (open=%d, len=%d)", slcan_opened, len);
            send_response("\x07");
        }
        break;

    case 'O': // Open CAN channel (normal mode)
        if (!slcan_opened)
        {
            twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
                CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
            twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

            g_config.tx_queue_len = 10;
            g_config.rx_queue_len = 20;

            if (twai_driver_install(&g_config, &current_timing, &f_config) == ESP_OK)
            {
                if (twai_start() == ESP_OK)
                {
                    slcan_opened = true;
                    slcan_listen_only = false;
                    send_response("\r");
                    ESP_LOGI(TAG, "CAN opened (normal mode)");
                }
                else
                {
                    twai_driver_uninstall();
                    send_response("\x07");
                    ESP_LOGE(TAG, "Failed to start CAN");
                }
            }
            else
            {
                send_response("\x07");
                ESP_LOGE(TAG, "Failed to install CAN driver");
            }
        }
        else
        {
            send_response("\x07");
            ESP_LOGW(TAG, "CAN already opened");
        }
        break;

    case 'L': // Open in listen-only mode
        if (!slcan_opened)
        {
            twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
                CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_LISTEN_ONLY);
            twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

            g_config.tx_queue_len = 10;
            g_config.rx_queue_len = 20;

            if (twai_driver_install(&g_config, &current_timing, &f_config) == ESP_OK)
            {
                if (twai_start() == ESP_OK)
                {
                    slcan_opened = true;
                    slcan_listen_only = true;
                    send_response("\r");
                    ESP_LOGI(TAG, "CAN opened (listen-only mode)");
                }
                else
                {
                    twai_driver_uninstall();
                    send_response("\x07");
                    ESP_LOGE(TAG, "Failed to start CAN (listen-only)");
                }
            }
            else
            {
                send_response("\x07");
                ESP_LOGE(TAG, "Failed to install CAN driver (listen-only)");
            }
        }
        else
        {
            send_response("\x07");
            ESP_LOGW(TAG, "CAN already opened");
        }
        break;

    case 'C': // Close CAN channel
        if (slcan_opened)
        {
            twai_stop();
            twai_driver_uninstall();
            slcan_opened = false;
            slcan_listen_only = false;
            send_response("\r");
            ESP_LOGI(TAG, "CAN closed");
        }
        else
        {
            send_response("\r"); // Some tools expect success even if already closed
            ESP_LOGD(TAG, "CAN already closed");
        }
        break;

    case 't': // Transmit standard frame
    case 'T': // Transmit extended frame
    case 'r': // Transmit standard RTR frame
    case 'R': // Transmit extended RTR frame
        if (slcan_opened && !slcan_listen_only)
        {
            twai_message_t msg = {0};
            int idx = 1;

            // Parse ID
            int id_len = (cmd[0] == 't' || cmd[0] == 'r') ? 3 : 8;
            if (len < idx + id_len + 1)
            {
                ESP_LOGW(TAG, "TX: Invalid length");
                send_response("\x07");
                break;
            }

            uint32_t id = 0;
            for (int i = 0; i < id_len; i++)
            {
                id = (id << 4) | hex_to_byte(cmd[idx++]);
            }
            msg.identifier = id;
            msg.extd = (cmd[0] == 'T' || cmd[0] == 'R') ? 1 : 0;
            msg.rtr = (cmd[0] == 'r' || cmd[0] == 'R') ? 1 : 0;

            // Parse DLC
            if (idx >= len)
            {
                ESP_LOGW(TAG, "TX: Missing DLC");
                send_response("\x07");
                break;
            }
            msg.data_length_code = hex_to_byte(cmd[idx++]);
            if (msg.data_length_code > 8)
            {
                ESP_LOGW(TAG, "TX: Invalid DLC %d", msg.data_length_code);
                send_response("\x07");
                break;
            }

            // Parse data (if not RTR)
            if (!msg.rtr)
            {
                if (len < idx + msg.data_length_code * 2)
                {
                    ESP_LOGW(TAG, "TX: Incomplete data");
                    send_response("\x07");
                    break;
                }
                for (int i = 0; i < msg.data_length_code; i++)
                {
                    msg.data[i] = (hex_to_byte(cmd[idx]) << 4) | hex_to_byte(cmd[idx + 1]);
                    idx += 2;
                }
            }

            // Transmit
            if (twai_transmit(&msg, pdMS_TO_TICKS(1000)) == ESP_OK)
            {
                send_response("z\r"); // Success
                ESP_LOGD(TAG, "TX: ID=0x%03X DLC=%d", msg.identifier, msg.data_length_code);
            }
            else
            {
                send_response("\x07"); // Error
                ESP_LOGW(TAG, "TX failed");
            }
        }
        else
        {
            ESP_LOGW(TAG, "TX: CAN not open or listen-only");
            send_response("\x07");
        }
        break;

    case 'Z': // Enable timestamps
        if (len >= 2)
        {
            slcan_timestamp = (cmd[1] == '1');
            send_response("\r");
            ESP_LOGI(TAG, "Timestamps %s", slcan_timestamp ? "enabled" : "disabled");
        }
        else
        {
            send_response("\x07");
        }
        break;

    case 'V': // Get hardware version
        send_response("V1234\r");
        break;

    case 'v': // Get firmware version
        send_response("v0100\r");
        break;

    case 'N': // Get serial number
        send_response("NESP32\r");
        break;

    case 'F': // Read status flags
        send_response("F00\r"); // No errors
        break;

    case 'W': // Filter mode (not implemented)
        send_response("\r");
        break;

    case 'M': // Acceptance code (not implemented)
        send_response("\r");
        break;

    case 'm': // Acceptance mask (not implemented)
        send_response("\r");
        break;

    default:
        ESP_LOGW(TAG, "Unknown command: %c", cmd[0]);
        send_response("\x07");
        break;
    }
}

// Task to receive CAN messages and send to USB CDC
static void can_rx_task(void *arg)
{
    twai_message_t msg;
    char buf[64];

    ESP_LOGI(TAG, "CAN RX task started");

    while (1)
    {
        if (slcan_opened)
        {
            if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK)
            {
                int idx = 0;

                // Frame type
                if (msg.extd)
                {
                    buf[idx++] = msg.rtr ? 'R' : 'T';
                }
                else
                {
                    buf[idx++] = msg.rtr ? 'r' : 't';
                }

                // ID
                int id_len = msg.extd ? 8 : 3;
                for (int i = id_len - 1; i >= 0; i--)
                {
                    buf[idx++] = byte_to_hex((msg.identifier >> (i * 4)) & 0xF);
                }

                // DLC
                buf[idx++] = byte_to_hex(msg.data_length_code);

                // Data (if not RTR)
                if (!msg.rtr)
                {
                    for (int i = 0; i < msg.data_length_code; i++)
                    {
                        buf[idx++] = byte_to_hex((msg.data[i] >> 4) & 0xF);
                        buf[idx++] = byte_to_hex(msg.data[i] & 0xF);
                    }
                }

                // Timestamp (if enabled)
                if (slcan_timestamp)
                {
                    uint32_t timestamp = xTaskGetTickCount();
                    buf[idx++] = byte_to_hex((timestamp >> 12) & 0xF);
                    buf[idx++] = byte_to_hex((timestamp >> 8) & 0xF);
                    buf[idx++] = byte_to_hex((timestamp >> 4) & 0xF);
                    buf[idx++] = byte_to_hex(timestamp & 0xF);
                }

                // Terminator
                buf[idx++] = '\r';
                buf[idx] = '\0';

                // Send via USB CDC
                tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t *)buf, idx);
                tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);

                ESP_LOGD(TAG, "RX: %s", buf);
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// USB CDC RX callback
void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    size_t rx_size = 0;

    esp_err_t ret = tinyusb_cdcacm_read(itf, cdc_rx_buf, CDC_RX_BUF_SIZE, &rx_size);
    if (ret == ESP_OK)
    {
        for (size_t i = 0; i < rx_size; i++)
        {
            char c = cdc_rx_buf[i];

            if (c == '\r' || c == '\n')
            {
                if (cmd_index > 0)
                {
                    cmd_buffer[cmd_index] = '\0';
                    process_slcan_command(cmd_buffer, cmd_index);
                    cmd_index = 0;
                }
            }
            else if (cmd_index < sizeof(cmd_buffer) - 1)
            {
                cmd_buffer[cmd_index++] = c;
            }
            else
            {
                // Buffer overflow - reset
                cmd_index = 0;
                send_response("\x07");
                ESP_LOGW(TAG, "Command buffer overflow");
            }
        }
    }
}

// USB CDC line state callback
void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "Line state changed: DTR=%d RTS=%d", dtr, rts);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32 SLCAN Adapter ===");
    ESP_LOGI(TAG, "CAN TX: GPIO%d, RX: GPIO%d", CAN_TX_GPIO, CAN_RX_GPIO);

    // Initialize with default 500kbit/s timing
    current_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();

    // Configure USB CDC
    ESP_LOGI(TAG, "Initializing USB CDC...");

    tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL, // Use default
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 256,
        .callback_rx = &tinyusb_cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = &tinyusb_cdc_line_state_changed_callback,
        .callback_line_coding_changed = NULL,
    };

    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    ESP_LOGI(TAG, "USB CDC configured");

    // Create CAN RX task
    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "SLCAN ready!");

    // Wait for USB enumeration
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Send ready message
    send_response("\r\nSLCAN ESP32 Ready\r\n");
    send_response("Use 'candump' or 'cansend' tools\r\n");
}
