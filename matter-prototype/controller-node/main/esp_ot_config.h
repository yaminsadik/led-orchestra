#pragma once

#include "sdkconfig.h"

#if CONFIG_OPENTHREAD_ENABLED
#include "esp_openthread_types.h"

// LED Orchestra controller radio config.
//
// Default (all-C6 controller and the C6 LED nodes): the native 802.15.4 radio.
//
// S3+H2 hub scaffold (NOT YET GATED): when this app is built as the esp-thread-br
// host on the ESP32-S3 (CONFIG_OPENTHREAD_BORDER_ROUTER +
// CONFIG_OPENTHREAD_RADIO_SPINEL_UART, via
// ../../s3-h2-hub-validation/sdkconfig.controller-node.s3-otbr.defaults), drive the
// on-board ESP32-H2 RCP over UART. Pins match the ESP Thread BR board and the
// stock esp-matter examples: rx=GPIO17 / tx=GPIO18 @ 460800; RCP update
// reset=GPIO7 / boot=GPIO8; bundled image dir /rcp_fw/ot_rcp. The Stage C gate is
// proven with the stock controller example first; this path is staged, not proven.
#if CONFIG_OPENTHREAD_BORDER_ROUTER && CONFIG_OPENTHREAD_RADIO_SPINEL_UART
#include <esp_rcp_update.h>

#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()              \
    {                                                      \
        .radio_mode = RADIO_MODE_UART_RCP,                 \
        .radio_uart_config = {                             \
            .port = UART_NUM_1,                            \
            .uart_config =                                 \
                {                                          \
                    .baud_rate = 460800,                   \
                    .data_bits = UART_DATA_8_BITS,         \
                    .parity = UART_PARITY_DISABLE,         \
                    .stop_bits = UART_STOP_BITS_1,         \
                    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, \
                    .rx_flow_ctrl_thresh = 0,              \
                    .source_clk = UART_SCLK_DEFAULT,       \
                },                                         \
            .rx_pin = GPIO_NUM_17,                         \
            .tx_pin = GPIO_NUM_18,                         \
        },                                                 \
    }

#define ESP_OPENTHREAD_RCP_UPDATE_CONFIG()                                                                                       \
    {                                                                                                                            \
        .rcp_type = RCP_TYPE_ESP32H2_UART, .uart_rx_pin = 17, .uart_tx_pin = 18, .uart_port = 1, .uart_baudrate = 115200,        \
        .reset_pin = 7, .boot_pin = 8, .update_baudrate = 460800, .firmware_dir = "/rcp_fw/ot_rcp", .target_chip = ESP32H2_CHIP, \
    }
#else
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG() \
    {                                         \
        .radio_mode = RADIO_MODE_NATIVE,      \
    }
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER && CONFIG_OPENTHREAD_RADIO_SPINEL_UART

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()               \
    {                                                      \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE, \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
