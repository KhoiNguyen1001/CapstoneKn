/**
 * Minimal Modbus RTU test, no library. STM32 HAL + MAX3491 (half-duplex strapped).
 * Sends: read input register 0x3100 (PV voltage) from slave 1, FC 0x04.
 * Adjust the pin names, huart handle, and baud rate to your project.
 */

#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart4;          /* your UART, 115200 8N1 */

/* Change these to your CubeMX pin names */
#define DE_PORT  RS485_DE_GPIO_Port
#define DE_PIN   RS485_DE_Pin
#define RE_PORT  RS485_RE_GPIO_Port        /* RE is active LOW */
#define RE_PIN   RS485_RE_Pin

#define SLAVE_ID 1

static uint16_t crc16(const uint8_t *d, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *d++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

/* Receive mode: receiver on first, then driver off */
static void rx_mode(void)
{
    HAL_GPIO_WritePin(RE_PORT, RE_PIN, GPIO_PIN_RESET);  /* RE low  = receiver on  */
    HAL_GPIO_WritePin(DE_PORT, DE_PIN, GPIO_PIN_RESET);  /* DE low  = driver off   */
}

/* Transmit mode: driver on first, then receiver off (never RE high + DE low = shutdown) */
static void tx_mode(void)
{
    HAL_GPIO_WritePin(DE_PORT, DE_PIN, GPIO_PIN_SET);    /* DE high = driver on    */
    HAL_GPIO_WritePin(RE_PORT, RE_PIN, GPIO_PIN_SET);    /* RE high = receiver off */
}

/* Returns number of bytes received (0 = nothing came back) */
static uint16_t modbus_read_input_reg(uint8_t id, uint16_t reg, uint16_t qty,
                                      uint8_t *rx, uint16_t rx_size)
{
    uint8_t req[8];
    req[0] = id;
    req[1] = 0x04;                    /* function: read input registers */
    req[2] = reg >> 8;
    req[3] = reg & 0xFF;
    req[4] = qty >> 8;
    req[5] = qty & 0xFF;
    uint16_t crc = crc16(req, 6);
    req[6] = crc & 0xFF;              /* CRC low byte first */
    req[7] = crc >> 8;

    /* Clear any stale errors/bytes */
    __HAL_UART_CLEAR_OREFLAG(&huart4);
    __HAL_UART_CLEAR_FEFLAG(&huart4);
    __HAL_UART_CLEAR_NEFLAG(&huart4);
    uint8_t junk;
    while (HAL_UART_Receive(&huart4, &junk, 1, 0) == HAL_OK) { }

    tx_mode();
    HAL_UART_Transmit(&huart4, req, sizeof(req), 100);   /* returns after TC flag */
    rx_mode();

    /* Expected reply for qty registers: 1 + 1 + 1 + 2*qty + 2 bytes.
     * Receive byte by byte until a gap, so a short or error reply still shows up. */
    uint16_t n = 0;
    uint32_t timeout = 200;           /* ms to wait for first byte */
    while (n < rx_size) {
        if (HAL_UART_Receive(&huart4, &rx[n], 1, timeout) != HAL_OK)
            break;
        n++;
        timeout = 10;                 /* inter-byte gap once the reply has started */
    }
    return n;
}

/* Call this from main() after MX_GPIO_Init() and MX_USART1_UART_Init() */
void modbus_simple_test(void)
{
    uint8_t rx[32];

    rx_mode();                        /* idle state: receiver on, driver off */

    while (1) {
        memset(rx, 0, sizeof(rx));
        uint16_t n = modbus_read_input_reg(SLAVE_ID, 0x3104, 1, rx, sizeof(rx));

        if (n == 0) {
            /* No reply: check wiring, A/B swap, baud, slave ID, MPPT power */
        } else if (n >= 7 && rx[0] == SLAVE_ID && rx[1] == 0x04 &&
                   crc16(rx, n - 2) == (uint16_t)(rx[n - 2] | (rx[n - 1] << 8))) {
            uint16_t raw = (rx[3] << 8) | rx[4];
            float pv_volts = raw / 100.0f;     /* EPEVER scale: 0.01 V per count */
            (void)pv_volts;                    /* watch in debugger */
        } else if (n == 5 && rx[1] == 0x84) {
            /* Modbus exception, code in rx[2] (2 = illegal address) */
        } else {
            /* Got bytes, but not a valid reply: inspect rx[] / n in the debugger */
        }

        HAL_Delay(1000);
    }
}