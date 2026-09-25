#ifndef BMS_H
#define BMS_H

#include "esp_err.h"
#include "driver/uart.h"

/* JK BMS link: UART2, TX = GPIO17, RX = GPIO16 (readme pin plan), 8N1.
 * Baud rate, address, register and RS485 DE pin: menuconfig -> BMS IO Automation -> JK BMS. */
#define BMS_UART_PORT     UART_NUM_2
#define BMS_UART_TX_PIN   GPIO_NUM_17
#define BMS_UART_RX_PIN   GPIO_NUM_16

/* Installs the UART and starts a task that reads the SoC every CONFIG_APP_BMS_POLL_MS
 * and stores it with app_state_set_soc(). After BMS_STALE_POLLS failed polls in a row
 * the SoC is marked unavailable (app_state_invalidate_soc()). */
esp_err_t bms_start(void);

#endif /* BMS_H */
