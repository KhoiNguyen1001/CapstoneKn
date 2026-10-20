/*
 * rtc_uart.c - Set / read a DS3231 from a PC over UART (STM32 HAL)
 *
 * FRAME FORMAT (PC -> STM32), 11 bytes for SET_TIME
 * ---------------------------------------------------------------
 *  Byte  Field     Notes
 *  0     SOF       0xAA
 *  1     CMD       0x01 = SET_TIME, 0x02 = GET_TIME
 *  2     LEN       payload length (7 for SET_TIME, 0 for GET_TIME)
 *  3..9  PAYLOAD   year(0-99), month(1-12), date(1-31), weekday(1-7, 1=Mon),
 *                  hour(0-23), minute(0-59), second(0-59)   -- plain binary
 *  last  CRC8      poly 0x07, init 0x00, computed over CMD, LEN, PAYLOAD
 *
 * RESPONSE (STM32 -> PC)
 *  SET_TIME : single byte 0x06 (ACK) or 0x15 (NAK)
 *  GET_TIME : frame with CMD = 0x82, LEN = 7, same payload layout, CRC8
 *
 * Usage:
 *   - Enable I2C1 and USART2 in CubeMX (change handles below if different).
 *   - Call rtc_uart_init() once after peripheral init.
 *   - Call rtc_uart_process() inside while(1).
 */

#include "main.h"
#include <string.h>
#include "../library/ds3231.h"

extern UART_HandleTypeDef huart2;    /* change to your UART handle */
extern DS3231_HandleTypeDef DS3231_Handler;

#define FRAME_SOF        0xAA
#define CMD_SET_TIME     0x01
#define CMD_GET_TIME     0x02
#define CMD_TIME_RESP    0x82
#define RESP_ACK         0x06
#define RESP_NAK         0x15

#define PAYLOAD_LEN      7
#define RX_TIMEOUT_MS    100

/* ---------- helpers ---------- */
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint8_t bcd2bin(uint8_t v) { return (uint8_t)(((v >> 4) * 10) + (v & 0x0F)); }

static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static uint8_t time_is_valid(const DS3231_TimeDate *t)
{
    return t->year <= 99 &&
           t->month  >= 1 && t->month <= 12 &&
           t->day   >= 1 && t->day  <= 31 &&
           t->dayOfWeek >= 1 && t->dayOfWeek <= 7 &&
           t->hours <= 23 && t->minutes <= 59 && t->seconds <= 59;
}

/* ---------- UART frame parser (runs in RX interrupt) ---------- */
typedef enum { ST_SOF, ST_CMD, ST_LEN, ST_DATA, ST_CRC } rx_state_t;

static volatile rx_state_t rx_state = ST_SOF;
static uint8_t  rx_byte;                    /* single-byte IT buffer   */
static uint8_t  rx_cmd, rx_len, rx_idx;
static uint8_t  rx_payload[PAYLOAD_LEN];
static volatile uint32_t last_rx_tick;

/* completed frame handed to main loop */
static volatile uint8_t frame_ready = 0;
static uint8_t frame_cmd, frame_len;
static uint8_t frame_data[PAYLOAD_LEN];

static void parser_feed(uint8_t b)
{
    last_rx_tick = HAL_GetTick();

    switch (rx_state) {
    case ST_SOF:
        if (b == FRAME_SOF) rx_state = ST_CMD;
        break;

    case ST_CMD:
        rx_cmd = b;
        rx_state = ST_LEN;
        break;

    case ST_LEN:
        if (b > PAYLOAD_LEN) { rx_state = ST_SOF; break; }   /* invalid */
        rx_len = b;
        rx_idx = 0;
        rx_state = (b == 0) ? ST_CRC : ST_DATA;
        break;

    case ST_DATA:
        rx_payload[rx_idx++] = b;
        if (rx_idx >= rx_len) rx_state = ST_CRC;
        break;

    case ST_CRC: {
        uint8_t tmp[2 + PAYLOAD_LEN];
        tmp[0] = rx_cmd;
        tmp[1] = rx_len;
        memcpy(&tmp[2], rx_payload, rx_len);

        if (crc8(tmp, (uint8_t)(2 + rx_len)) == b && !frame_ready) {
            frame_cmd = rx_cmd;
            frame_len = rx_len;
            memcpy(frame_data, rx_payload, rx_len);
            frame_ready = 1;
        } else if (crc8(tmp, (uint8_t)(2 + rx_len)) != b) {
            /* bad CRC: tell the PC (safe to ignore if TX in ISR is a concern) */
            frame_cmd = 0xFF;           /* flag NAK for main loop */
            frame_len = 0;
            frame_ready = 1;
        }
        rx_state = ST_SOF;
        break;
    }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == huart2.Instance) {
        parser_feed(rx_byte);
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == huart2.Instance) {
        rx_state = ST_SOF;                          /* resync */
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1);  /* re-arm */
    }
}

/* ---------- public API ---------- */
void rtc_uart_init(void)
{
    rx_state = ST_SOF;
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

/* call from main loop */
void rtc_uart_process(DS3231_TimeDate *t)
{
    /* drop a half-received frame if the sender stalled */
    if (rx_state != ST_SOF && (HAL_GetTick() - last_rx_tick) > RX_TIMEOUT_MS)
        rx_state = ST_SOF;

    if (!frame_ready) return;

    uint8_t cmd = frame_cmd, len = frame_len;
    memcpy(t, frame_data, sizeof(DS3231_TimeDate) < len ? sizeof(DS3231_TimeDate) : len);
    frame_ready = 0;

    switch (cmd) {
    case CMD_SET_TIME:
        if (len == PAYLOAD_LEN && time_is_valid(t) &&
            DS3231_SetTimeDate(&DS3231_Handler, t) == HAL_OK)
            //no need to send response, check routine status
        //     send_resp(RESP_ACK);
        // else
        //     send_resp(RESP_NAK);
        break;

    default:            /* unknown command or CRC error */
        // send_resp(RESP_NAK);
        break;
    }
}
