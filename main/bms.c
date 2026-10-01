#include <stdbool.h>
#include <stdint.h>
#include <string.h>
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

#define BMS_RX_BUF_SIZE      256   /* driver ring buffer, must be larger than the 128-byte UART FIFO */
#define BMS_FRAME_MAX        64    /* bytes collected per answer (reply + possible echo/noise) */
#define BMS_FRAME_GAP_MS     20    /* silence that ends a frame (STM32 used 5 ms; FreeRTOS tick is 10 ms) */
#define BMS_STALE_POLLS      10    /* SoC marked unavailable after this many failed polls in a row */
#define BMS_REPEAT_LOG_POLLS 60    /* while failing, log the raw bytes again every this many polls */
#define BMS_TASK_STACK       3072
#define BMS_TASK_PRIO        9

_Static_assert(CONFIG_APP_BMS_RESPONSE_TIMEOUT_MS + BMS_FRAME_GAP_MS < CONFIG_APP_BMS_POLL_MS,
               "response timeout must be shorter than the poll period");

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

static bool crc_ok(const uint8_t *frame, size_t len_with_crc)
{
    uint16_t rx = (uint16_t)(frame[len_with_crc - 2] | (frame[len_with_crc - 1] << 8));
    return modbus_crc16(frame, len_with_crc - 2) == rx;
}

/* Same frame as GenerateReadRequest() in the STM32 project: read 1 holding register */
static void build_request(uint8_t req[MB_REQ_LEN], uint8_t slave, uint16_t reg)
{
    req[0] = slave;
    req[1] = MB_FC_READ_HOLDING;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = 0x00;                  /* number of registers - high byte */
    req[5] = 0x01;                  /* number of registers - low byte */
    uint16_t crc = modbus_crc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);
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

/* Collects one burst of bytes like the STM32 did: wait up to first_byte_wait for the first
 * byte, then keep reading until the line has been silent for BMS_FRAME_GAP_MS.
 * Returns the number of bytes received (0 = nothing), or -1 on a driver error. */
static int receive_frame(uint8_t *buf, size_t size, TickType_t first_byte_wait)
{
    int n = uart_read_bytes(BMS_UART_PORT, buf, 1, first_byte_wait);
    if (n <= 0)
    {
        return n;
    }

    TickType_t gap = pdMS_TO_TICKS(BMS_FRAME_GAP_MS);
    if (gap == 0)
    {
        gap = 1;
    }
    size_t got = (size_t)n;
    while (got < size)
    {
        n = uart_read_bytes(BMS_UART_PORT, buf + got, size - got, gap);
        if (n < 0)
        {
            return -1;
        }
        if (n == 0)
        {
            break;                  /* line silent: frame complete */
        }
        got += (size_t)n;
    }
    return (int)got;
}

/* Searches the received bytes for a valid answer from `slave`. Stray bytes before the answer
 * (a turn-around glitch, or an echo of our own request) are skipped. */
static esp_err_t parse_answer(const uint8_t *buf, size_t len, uint8_t slave, uint16_t *value)
{
    for (size_t i = 0; i + MB_EXC_LEN <= len; i++)
    {
        /* Address 0 is what the reference project used; accept whatever address answers then */
        if (slave != 0 && buf[i] != slave)
        {
            continue;
        }
        if (buf[i + 1] == MB_FC_READ_HOLDING && buf[i + 2] == 2 &&
            i + MB_RESP_LEN_1REG <= len && crc_ok(&buf[i], MB_RESP_LEN_1REG))
        {
            if (i > 0)
            {
                ESP_LOGD(TAG, "Skipped %u stray byte(s) before the answer", (unsigned)i);
            }
            *value = (uint16_t)((buf[i + 3] << 8) | buf[i + 4]);
            return ESP_OK;
        }
        if (buf[i + 1] == (MB_FC_READ_HOLDING | 0x80) && crc_ok(&buf[i], MB_EXC_LEN))
        {
            ESP_LOGW(TAG, "Device answered with Modbus exception %u%s", buf[i + 2],
                     buf[i + 2] == 2 ? " (illegal data address: wrong register for this device)" : "");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_ERR_INVALID_CRC;     /* bytes arrived, but no valid frame among them */
}

/* One request/answer cycle. rx/rx_len return the raw bytes for diagnostics.
 * Like the STM32 (which kept receiving between polls), bursts are collected for the whole
 * response timeout: an echo of our own request from the RS485 module is skipped and a late
 * answer after it is still found. *echo_only is set when nothing but that echo came back. */
static esp_err_t read_holding_register(const uint8_t req[MB_REQ_LEN], uint8_t slave, uint16_t *value,
                                       uint8_t *rx, int *rx_len, bool *echo_only)
{
    *rx_len = 0;
    *echo_only = false;

    /* Drop leftovers of an earlier, late or broken answer */
    esp_err_t err = uart_flush_input(BMS_UART_PORT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_flush_input failed (%s)", esp_err_to_name(err));
        return err;
    }

    int written = uart_write_bytes(BMS_UART_PORT, req, MB_REQ_LEN);
    if (written != MB_REQ_LEN)
    {
        ESP_LOGE(TAG, "uart_write_bytes wrote %d of %d bytes", written, MB_REQ_LEN);
        return ESP_FAIL;
    }
    err = uart_wait_tx_done(BMS_UART_PORT, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_wait_tx_done failed (%s)", esp_err_to_name(err));
        return err;
    }
    const TickType_t sent_at = xTaskGetTickCount();

#if CONFIG_APP_BMS_TRACE
    /* The UART driver buffers incoming bytes, so logging here loses no part of the answer */
    ESP_LOGI(TAG, "TX %d bytes:", MB_REQ_LEN);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, req, MB_REQ_LEN, ESP_LOG_INFO);
#endif

    const TickType_t timeout = pdMS_TO_TICKS(CONFIG_APP_BMS_RESPONSE_TIMEOUT_MS);
    const TickType_t start = sent_at;
    size_t total = 0;
    esp_err_t result = ESP_ERR_TIMEOUT;

    while (total < BMS_FRAME_MAX)
    {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout)
        {
            break;
        }
        int n = receive_frame(rx + total, BMS_FRAME_MAX - total, timeout - elapsed);
        if (n < 0)
        {
            ESP_LOGE(TAG, "uart_read_bytes failed");
            *rx_len = (int)total;
            return ESP_FAIL;
        }
        if (n == 0)
        {
            break;                          /* nothing more before the timeout */
        }
        total += (size_t)n;

        result = parse_answer(rx, total, slave, value);
        if (result == ESP_OK || result == ESP_ERR_INVALID_RESPONSE)
        {
            break;                          /* valid answer, or a definite error answer */
        }
    }

    *rx_len = (int)total;

#if CONFIG_APP_BMS_TRACE
    if (total == 0)
    {
        ESP_LOGI(TAG, "RX: nothing within %d ms", CONFIG_APP_BMS_RESPONSE_TIMEOUT_MS);
    }
    else
    {
        ESP_LOGI(TAG, "RX %u bytes after %lu ms:", (unsigned)total,
                 (unsigned long)pdTICKS_TO_MS(xTaskGetTickCount() - sent_at));
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx, total, ESP_LOG_INFO);
    }
#endif

    if (total == 0)
    {
        return ESP_ERR_TIMEOUT;
    }
    if (result != ESP_OK && result != ESP_ERR_INVALID_RESPONSE &&
        total == MB_REQ_LEN && memcmp(rx, req, MB_REQ_LEN) == 0)
    {
        *echo_only = true;                  /* only our own request came back */
        return ESP_ERR_TIMEOUT;
    }
    return result;
}

static void log_failure(esp_err_t err, const uint8_t *rx, int rx_len, bool echo_only, uint32_t fails)
{
    if (echo_only)
    {
        ESP_LOGW(TAG, "Only the echo of our own request came back, no answer within %d ms (failed polls: %lu) - "
                      "the request reaches the bus but the BMS does not reply: check A/B swap, GND to the "
                      "BMS RS485 port, BMS port/protocol and address", CONFIG_APP_BMS_RESPONSE_TIMEOUT_MS,
                 (unsigned long)fails);
        return;
    }
    if (err == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "No answer within %d ms (failed polls: %lu) - check wiring (TX2/RX2 swap, A/B swap), "
                      "baud rate and address", CONFIG_APP_BMS_RESPONSE_TIMEOUT_MS, (unsigned long)fails);
        return;
    }
    ESP_LOGW(TAG, "Invalid answer (%s), %d byte(s) received (failed polls: %lu):",
             esp_err_to_name(err), rx_len, (unsigned long)fails);
    if (rx_len > 0)
    {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx, rx_len, ESP_LOG_WARN);
    }
}

static void bms_task(void *arg)
{
    (void)arg;
    const uint8_t slave = CONFIG_APP_BMS_SLAVE_ID;
    const uint16_t mask = CONFIG_APP_BMS_SOC_MASK;
    const int shift = __builtin_ctz(mask);
    uint8_t req[MB_REQ_LEN];
    uint8_t rx[BMS_FRAME_MAX];
    uint32_t fails = 0;
    int last_soc = -1;
    TickType_t last_wake = xTaskGetTickCount();

    build_request(req, slave, CONFIG_APP_BMS_SOC_REGISTER);
    ESP_LOGI(TAG, "Request frame (compare with the STM32 RequestBuffer):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, req, MB_REQ_LEN, ESP_LOG_INFO);

    while (1)
    {
        uint16_t reg = 0;
        int rx_len = 0;
        bool echo_only = false;
        esp_err_t err = read_holding_register(req, slave, &reg, rx, &rx_len, &echo_only);
        if (err == ESP_OK)
        {
            int soc = (reg & mask) >> shift;
            if (soc > 100)
            {
                ESP_LOGW(TAG, "Implausible SoC %d (register value 0x%04X) - check register and mask", soc, reg);
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
                    ESP_LOGI(TAG, "SoC %d %% (register value 0x%04X)", soc, reg);
                    last_soc = soc;
                }
            }
        }

        if (err != ESP_OK)
        {
            fails++;
            /* Log the first failure, the moment the SoC goes stale, then once a minute */
            if (fails == 1 || fails == BMS_STALE_POLLS || (fails % BMS_REPEAT_LOG_POLLS) == 0)
            {
                log_failure(err, rx, rx_len, echo_only, fails);
            }
            if (fails == BMS_STALE_POLLS)
            {
                ESP_LOGE(TAG, "No valid SoC for %d polls - SoC unavailable, boiler and triggers OFF",
                         BMS_STALE_POLLS);
                app_state_invalidate_soc();
                last_soc = -1;
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
    ESP_LOGI(TAG, "Polling address %d, register %d (0x%04X), mask 0x%04X every %d ms, timeout %d ms",
             CONFIG_APP_BMS_SLAVE_ID, CONFIG_APP_BMS_SOC_REGISTER, CONFIG_APP_BMS_SOC_REGISTER,
             CONFIG_APP_BMS_SOC_MASK, CONFIG_APP_BMS_POLL_MS, CONFIG_APP_BMS_RESPONSE_TIMEOUT_MS);
    return ESP_OK;
}
