/* UART Echo Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "uart_helper.h"
#include "../../hal_config.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "esp_log.h"

/**
 * This is an example which echos any data it receives on configured UART back to the sender,
 * with hardware flow control turned off. It does not use UART driver event queue.
 *
 * - Port: configured UART
 * - Receive (Rx) buffer: on
 * - Transmit (Tx) buffer: off
 * - Flow control: off
 * - Event queue: off
 * - Pin assignment: see defines below (See Kconfig)
 */

#define ECHO_TEST_TXD (HAL_PIN_GPS_TX)
#define ECHO_TEST_RXD (HAL_PIN_GPS_RX)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM           (2)
#define ECHO_TASK_STACK_SIZE         (4096)
#define UART_READ_TIMEOUT_MS         (200)
#define GPS_BAUD_PROBE_INTERVAL_MS   (3000)

static const char *TAG = "UartHelper";

#define BUF_SIZE (512)

static gps_uart_helper_on_msg_callback_t _on_msg = NULL;
static TaskHandle_t _task_handle                 = NULL;
static bool _uart_driver_installed               = false;

static const int _gps_baud_rates[] = {
    115200,
    9600,
    38400,
    57600,
};
static size_t _gps_baud_index = 0;

static bool has_nmea_candidate(const uint8_t *data, int len)
{
    if (len <= 0) {
        return false;
    }

    int printable_count = 0;
    for (int i = 0; i < len; ++i) {
        uint8_t c = data[i];
        if (c == '\r' || c == '\n' || (c >= 0x20 && c <= 0x7E)) {
            ++printable_count;
        }
    }
    if (printable_count * 100 < len * 80) {
        return false;
    }

    for (int i = 0; i + 2 < len; ++i) {
        if (data[i] == '$' && (data[i + 1] == 'G' || data[i + 1] == 'P')) {
            return true;
        }
    }
    return false;
}

static esp_err_t configure_uart(int baud_rate)
{
    uart_config_t uart_config = {
        .baud_rate  = baud_rate,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    if (!_uart_driver_installed) {
        esp_err_t err = uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "uart driver install failed: %s", esp_err_to_name(err));
            return err;
        }
        _uart_driver_installed = true;
    }

    esp_err_t err = uart_param_config(ECHO_UART_PORT_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart param config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart set pin failed: %s", esp_err_to_name(err));
        return err;
    }

    uart_flush_input(ECHO_UART_PORT_NUM);
    ESP_LOGI(TAG, "gps uart baud: %d", baud_rate);
    return ESP_OK;
}

static void echo_task(void *arg)
{
    ESP_LOGI(TAG, "gps uart helper task start");

    gpio_reset_pin(ECHO_TEST_TXD);
    gpio_reset_pin(ECHO_TEST_RXD);

    if (configure_uart(_gps_baud_rates[_gps_baud_index]) != ESP_OK) {
        _task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
    if (!data) {
        ESP_LOGE(TAG, "failed to allocate uart buffer");
        _task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    TickType_t last_nmea_tick = xTaskGetTickCount();

    while (1) {
        // Read data from the UART
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, (BUF_SIZE - 1), pdMS_TO_TICKS(UART_READ_TIMEOUT_MS));
        // Write data back to the UART
        //        uart_write_bytes(ECHO_UART_PORT_NUM, (const char *) data, len);
        if (len) {
            data[len] = '\0';
            // ESP_LOGI(TAG, "Recv str: %s", (char *)data);

            if (has_nmea_candidate(data, len)) {
                last_nmea_tick = xTaskGetTickCount();
            }

            if (_on_msg) {
                _on_msg((const char *)data);
            }
        }

        if (xTaskGetTickCount() - last_nmea_tick >= pdMS_TO_TICKS(GPS_BAUD_PROBE_INTERVAL_MS)) {
            _gps_baud_index = (_gps_baud_index + 1) % (sizeof(_gps_baud_rates) / sizeof(_gps_baud_rates[0]));
            configure_uart(_gps_baud_rates[_gps_baud_index]);
            last_nmea_tick = xTaskGetTickCount();
        }
    }
}

void gps_uart_helper_init()
{
    if (_task_handle) {
        ESP_LOGI(TAG, "gps uart helper already running");
        return;
    }

    if (xTaskCreate(echo_task, "gps_uart", ECHO_TASK_STACK_SIZE, NULL, 10, &_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "failed to create gps uart helper task");
        _task_handle = NULL;
    }
}

void gps_uart_helper_set_on_msg_callback(gps_uart_helper_on_msg_callback_t callback)
{
    _on_msg = callback;
}
