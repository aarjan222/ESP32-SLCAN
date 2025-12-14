#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/twai.h"
#include "driver/uart.h"
#include "esp_log.h"

#define TAG "SLCAN"

// CAN pins
#define CAN_TX_GPIO 21
#define CAN_RX_GPIO 22

// USB UART (used for programming)
#define UART_NUM UART_NUM_0
#define UART_BUF_SIZE 1024

// SLCAN states
static bool slcan_opened = false;
static bool slcan_listen_only = false;

// Queues
static QueueHandle_t rx_queue;
static QueueHandle_t tx_queue;

// Current timing configuration
static twai_timing_config_t current_timing;

// Get timing config by bitrate code
static bool get_timing_config(char code, twai_timing_config_t *config)
{
    twai_timing_config_t temp_config;

    switch (code)
    {
    case '0': // 10 kbit/s - custom config
        temp_config = (twai_timing_config_t){
            .clk_src = TWAI_CLK_SRC_DEFAULT,
            .quanta_resolution_hz = 1000000,
            .brp = 0,
            .tseg_1 = 63,
            .tseg_2 = 16,
            .sjw = 16,
            .triple_sampling = false};
        break;
    case '1': // 20 kbit/s - custom config
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

static void send_response(const char *resp)
{
    uart_write_bytes(UART_NUM, resp, strlen(resp));
}

// Parse SLCAN command and execute
static void process_slcan_command(const char *cmd, size_t len)
{
    if (len < 1)
        return;

    switch (cmd[0])
    {
    case 'S': // Set bitrate
        if (len >= 2 && !slcan_opened)
        {
            if (get_timing_config(cmd[1], &current_timing))
            {
                send_response("\r");
            }
            else
            {
                send_response("\x07");
            }
        }
        else
        {
            send_response("\x07");
        }
        break;

    case 'O': // Open CAN channel
        if (!slcan_opened)
        {
            twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
            twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

            if (slcan_listen_only)
            {
                g_config.mode = TWAI_MODE_LISTEN_ONLY;
            }

            if (twai_driver_install(&g_config, &current_timing, &f_config) == ESP_OK)
            {
                if (twai_start() == ESP_OK)
                {
                    slcan_opened = true;
                    send_response("\r");
                    ESP_LOGI(TAG, "CAN opened");
                }
                else
                {
                    twai_driver_uninstall();
                    send_response("\x07");
                }
            }
            else
            {
                send_response("\x07");
            }
        }
        else
        {
            send_response("\x07");
        }
        break;

    case 'L': // Open in listen-only mode
        if (!slcan_opened)
        {
            slcan_listen_only = true;
            twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_LISTEN_ONLY);
            twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

            if (twai_driver_install(&g_config, &current_timing, &f_config) == ESP_OK)
            {
                if (twai_start() == ESP_OK)
                {
                    slcan_opened = true;
                    send_response("\r");
                    ESP_LOGI(TAG, "CAN opened (listen-only)");
                }
                else
                {
                    twai_driver_uninstall();
                    send_response("\x07");
                }
            }
            else
            {
                send_response("\x07");
            }
        }
        else
        {
            send_response("\x07");
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
            send_response("\x07");
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
                send_response("\x07");
                break;
            }
            msg.data_length_code = hex_to_byte(cmd[idx++]);
            if (msg.data_length_code > 8)
            {
                send_response("\x07");
                break;
            }

            // Parse data
            if (!msg.rtr)
            {
                if (len < idx + msg.data_length_code * 2)
                {
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
            if (twai_transmit(&msg, pdMS_TO_TICKS(100)) == ESP_OK)
            {
                send_response("z\r");
            }
            else
            {
                send_response("\x07");
            }
        }
        else
        {
            send_response("\x07");
        }
        break;

    case 'V': // Get version
        send_response("V1234\r");
        break;

    case 'v': // Get minor version
        send_response("v0100\r");
        break;

    case 'N': // Get serial number
        send_response("NESP32\r");
        break;

    case 'F': // Status flags
        send_response("F00\r");
        break;

    default:
        send_response("\x07");
        break;
    }
}

// Task to receive CAN messages and send to UART
static void can_rx_task(void *arg)
{
    twai_message_t msg;
    char buf[64];

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

                // Data
                if (!msg.rtr)
                {
                    for (int i = 0; i < msg.data_length_code; i++)
                    {
                        buf[idx++] = byte_to_hex((msg.data[i] >> 4) & 0xF);
                        buf[idx++] = byte_to_hex(msg.data[i] & 0xF);
                    }
                }

                // Terminator
                buf[idx++] = '\r';
                buf[idx] = '\0';

                uart_write_bytes(UART_NUM, buf, idx);
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Task to receive UART commands
static void uart_rx_task(void *arg)
{
    uint8_t data[128];
    static char cmd_buf[128];
    static int cmd_idx = 0;

    while (1)
    {
        int len = uart_read_bytes(UART_NUM, data, sizeof(data), pdMS_TO_TICKS(20));

        for (int i = 0; i < len; i++)
        {
            if (data[i] == '\r' || data[i] == '\n')
            {
                if (cmd_idx > 0)
                {
                    cmd_buf[cmd_idx] = '\0';
                    process_slcan_command(cmd_buf, cmd_idx);
                    cmd_idx = 0;
                }
            }
            else if (cmd_idx < sizeof(cmd_buf) - 1)
            {
                cmd_buf[cmd_idx++] = data[i];
            }
            else
            {
                // Buffer overflow
                cmd_idx = 0;
                send_response("\x07");
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "SLCAN Device Starting...");

    // Initialize with default 500kbit/s timing
    current_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();

    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    uart_param_config(UART_NUM, &uart_config);
    uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 0, NULL, 0);

    ESP_LOGI(TAG, "UART configured at 115200 baud");
    ESP_LOGI(TAG, "CAN TX: GPIO%d, RX: GPIO%d", CAN_TX_GPIO, CAN_RX_GPIO);

    // Create tasks
    xTaskCreate(uart_rx_task, "uart_rx", 4096, NULL, 10, NULL);
    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 9, NULL);

    ESP_LOGI(TAG, "SLCAN ready!");
    send_response("\r\nSLCAN Ready\r\n");
}