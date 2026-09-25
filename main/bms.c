#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "app_state.h"
#include "bms.h"

static const char *TAG = "BMS";

#define MB_FC_READ_HOLDING   0x03
#define MB_REQ_LEN           8     /* addr, fc, reg hi, reg lo, count hi, count lo, crc lo, crc hi */
#define MB_RESP_LEN_1REG     7     /* addr, fc, byte count (2), value hi, value lo, crc lo, crc hi */
#define MB_EXC_LEN           5     /* addr, fc | 0x80, exception code, crc lo, crc hi */

#define BMS_RX_BUF_SIZE      256   /* must be larger than the 128-byte UART hardware FIFO */
#define BMS_STALE_POLLS      10    /* SoC marked unavailable after this many failed polls in a row */
#define BMS_TASK_STACK       3072
#define BMS_TASK_PRIO        9

/* Modbus RTU CRC-16 (poly 0xA001 reflected, init 0xFFFF), sent low byte first */
static uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static esp_err_t uart_setup(void)
{
    const uart_config_t cfg = {
        .baud_rate = CONFIG_APP_BMS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(BMS_UART_PORT, BMS_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_driver_install failed (%s)", esp_err_to_name(err));
        return err;
    }
    err = uart_param_config(BMS_UART_PORT, &cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_param_config failed (%s)", esp_err_to_name(err));
        return err;
    }

    /* With a MAX3485-style transceiver, the UART's RTS line drives DE and /RE */
    const int de_pin = CONFIG_APP_BMS_RS485_DE_GPIO;
    err = uart_set_pin(BMS_UART_PORT, BMS_UART_TX_PIN, BMS_UART_RX_PIN,
                       de_pin >= 0 ? de_pin : UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_set_pin failed (%s)", esp_err_to_name(err));
        return err;
    }
    if (de_pin >= 0)
    {
        err = uart_set_mode(BMS_UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "uart_set_mode(RS485) failed (%s)", esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "UART2 ready: TX=GPIO%d RX=GPIO%d %d 8N1, %s", BMS_UART_TX_PIN, BMS_UART_RX_PIN,
             CONFIG_APP_BMS_BAUD, de_pin >= 0 ? "RS485 DE via RTS pin" : "auto-direction transceiver");
    if (de_pin >= 0)
    {
        ESP_LOGI(TAG, "RS485 DE/RE on GPIO%d", de_pin);
    }
    return ESP_OK;
}

/* Modbus RTU "read holding registers" for ONE register. Blocks up to the response timeout. */
static esp_err_t read_holding_register(uint8_t slave, uint16_t reg, uint16_t *value)
{
    uint8_t req[MB_REQ_LEN] = {
        slave, MB_FC_READ_HOLDING,
        (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
        0x00, 0x01,                 /* number of registers */
    };
    uint16_t crc = modbus_crc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    /* Drop leftovers of an earlier, late or broken answer */
    esp_err_t err = uart_flush_input(BMS_UART_PORT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_flush_input failed (%s)", esp_err_to_name(err));
        return err;
    }

    int written = uart_write_bytes(BMS_UART_PORT, req, sizeof(req));
    if (written != (int)sizeof(req))
    {
        ESP_LOGE(TAG, "uart_write_bytes wrote %d of %u bytes", written, (unsigned)sizeof(req));
        return ESP_FAIL;
    }
    err = uart_wait_tx_done(BMS_UART_PORT, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_wait_tx_done failed (%s)", esp_err_to_name(err));
        return err;
    }

    /* Read until the frame is complete: 7 bytes normally, 5 for an exception answer */
    uint8_t resp[MB_RESP_LEN_1REG];
    size_t got = 0;
    size_t expected = MB_RESP_LEN_1REG;
    const TickType_t timeout = pdMS_TO_TICKS(CONFIG_APP_BMS_RESPONSE_TIMEOUT_MS);
    const TickType_t start = xTaskGetTickCount();

    while (got < expected)
    {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout)
        {
            break;
        }
        int n = uart_read_bytes(BMS_UART_PORT, resp + got, expected - got, timeout - elapsed);
        if (n < 0)
        {
            ESP_LOGE(TAG, "uart_read_bytes failed");
            return ESP_FAIL;
        }
        got += (size_t)n;
        if (got >= 2 && (resp[1] & 0x80))
        {
            expected = MB_EXC_LEN;
        }
    }

    if (got < expected)
    {
        return (got == 0) ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_SIZE;
    }

    uint16_t crc_rx = (uint16_t)(resp[expected - 2] | (resp[expected - 1] << 8));
    if (modbus_crc16(resp, expected - 2) != crc_rx)
    {
        return ESP_ERR_INVALID_CRC;
    }
    /* Address 0 is what the reference project used; accept whatever address answers then */
    if (slave != 0 && resp[0] != slave)
    {
        ESP_LOGW(TAG, "Answer from address %u, expected %u", resp[0], slave);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (resp[1] & 0x80)
    {
        ESP_LOGW(TAG, "BMS returned Modbus exception %u", resp[2]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (resp[1] != MB_FC_READ_HOLDING || resp[2] != 2)
    {
        ESP_LOGW(TAG, "Unexpected answer: function 0x%02X, %u data bytes", resp[1], resp[2]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *value = (uint16_t)((resp[3] << 8) | resp[4]);
    return ESP_OK;
}

static void bms_task(void *arg)
{
    (void)arg;
    const uint16_t mask = CONFIG_APP_BMS_SOC_MASK;
    const int shift = __builtin_ctz(mask);
    int fails = 0;
    int last_soc = -1;
    TickType_t last_wake = xTaskGetTickCount();

    while (1)
    {
        uint16_t reg = 0;
        esp_err_t err = read_holding_register(CONFIG_APP_BMS_SLAVE_ID, CONFIG_APP_BMS_SOC_REGISTER, &reg);
        if (err == ESP_OK)
        {
            int soc = (reg & mask) >> shift;
            if (soc > 100)
            {
                ESP_LOGW(TAG, "Implausible SoC %d (register 0x%04X) - check register and mask", soc, reg);
                err = ESP_ERR_INVALID_RESPONSE;
            }
            else
            {
                if (fails >= BMS_STALE_POLLS)
                {
                    ESP_LOGI(TAG, "BMS communication restored");
                }
                fails = 0;
                app_state_set_soc(soc);
                if (soc != last_soc)
                {
                    ESP_LOGI(TAG, "SoC %d %%", soc);
                    last_soc = soc;
                }
            }
        }

        if (err != ESP_OK)
        {
            if (fails < BMS_STALE_POLLS)
            {
                fails++;
                if (fails == 1)
                {
                    ESP_LOGW(TAG, "SoC read failed (%s)", esp_err_to_name(err));
                }
                if (fails == BMS_STALE_POLLS)
                {
                    ESP_LOGE(TAG, "No valid SoC for %d polls - SoC unavailable, triggers OFF", BMS_STALE_POLLS);
                    app_state_invalidate_soc();
                    last_soc = -1;
                }
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_APP_BMS_POLL_MS));
    }
}

esp_err_t bms_start(void)
{
    esp_err_t err = uart_setup();
    if (err != ESP_OK)
    {
        return err;
    }
    if (xTaskCreate(bms_task, "bms", BMS_TASK_STACK, NULL, BMS_TASK_PRIO, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create BMS task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Polling address %d, register 0x%04X, mask 0x%04X every %d ms",
             CONFIG_APP_BMS_SLAVE_ID, CONFIG_APP_BMS_SOC_REGISTER, CONFIG_APP_BMS_SOC_MASK,
             CONFIG_APP_BMS_POLL_MS);
    return ESP_OK;
}
