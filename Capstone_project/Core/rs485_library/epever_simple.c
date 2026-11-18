/**
 * @file epever_simple.c
 * @brief See epever_simple.h.
 */

#include "epever_simple.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* RS-485 direction control (MAX3491 half-duplex)                      */
/* Never goes through "RE high + DE low" (that is shutdown mode).      */
/* ------------------------------------------------------------------ */

static void rx_mode(EpeverS_t *h)
{
    HAL_GPIO_WritePin(h->re_port, h->re_pin, GPIO_PIN_RESET);  /* receiver on first */
    HAL_GPIO_WritePin(h->de_port, h->de_pin, GPIO_PIN_RESET);  /* then driver off   */
}

static void tx_mode(EpeverS_t *h)
{
    HAL_GPIO_WritePin(h->de_port, h->de_pin, GPIO_PIN_SET);    /* driver on first   */
    HAL_GPIO_WritePin(h->re_port, h->re_pin, GPIO_PIN_SET);    /* then receiver off */
}

/* ------------------------------------------------------------------ */
/* CRC                                                                 */
/* ------------------------------------------------------------------ */

uint16_t EpeverS_CRC16(const uint8_t *d, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *d++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* Core transaction                                                    */
/* ------------------------------------------------------------------ */

static void flush_rx(EpeverS_t *h)
{
    uint8_t junk;
    __HAL_UART_CLEAR_OREFLAG(h->huart);
    __HAL_UART_CLEAR_FEFLAG(h->huart);
    __HAL_UART_CLEAR_NEFLAG(h->huart);
    for (uint8_t i = 0; i < 64; i++) {
        if (HAL_UART_Receive(h->huart, &junk, 1, 0) != HAL_OK) break;
    }
    __HAL_UART_CLEAR_OREFLAG(h->huart);
    __HAL_UART_CLEAR_FEFLAG(h->huart);
    __HAL_UART_CLEAR_NEFLAG(h->huart);
}

/**
 * Send req (CRC already appended), collect the reply into rx, and check
 * address, CRC and exception. Function-specific length checks are done by
 * the callers. Returns the number of received bytes in *rx_len.
 */
static EpeverS_Status_t transact(EpeverS_t *h, const uint8_t *req, uint16_t req_len,
                                 uint8_t *rx, uint16_t rx_size, uint16_t *rx_len)
{
    HAL_Delay(EPEVERS_INTERFRAME_MS);
    flush_rx(h);

    tx_mode(h);
    HAL_StatusTypeDef st = HAL_UART_Transmit(h->huart, (uint8_t *)req, req_len, 100);
    rx_mode(h);
    if (st != HAL_OK) return EPEVERS_ERR_TX;

    /* Collect bytes until a gap: long wait for the first, short after. */
    uint16_t n = 0;
    uint32_t timeout = h->reply_timeout_ms;
    while (n < rx_size) {
        if (HAL_UART_Receive(h->huart, &rx[n], 1, timeout) != HAL_OK) break;
        n++;
        timeout = 10;
    }
    *rx_len = n;

    if (n == 0) return EPEVERS_ERR_TIMEOUT;
    if (n < 5)  return EPEVERS_ERR_FRAME;

    uint16_t crc_calc = EpeverS_CRC16(rx, (uint16_t)(n - 2));
    uint16_t crc_recv = (uint16_t)(rx[n - 2] | (rx[n - 1] << 8));
    if (crc_calc != crc_recv) return EPEVERS_ERR_CRC;

    if (rx[0] != h->slave_id) return EPEVERS_ERR_FRAME;

    if (rx[1] == (uint8_t)(req[1] | 0x80)) {
        h->last_exception = rx[2];
        return EPEVERS_ERR_EXCEPTION;
    }
    if (rx[1] != req[1]) return EPEVERS_ERR_FRAME;

    return EPEVERS_OK;
}

static void put_crc(uint8_t *buf, uint16_t len)
{
    uint16_t crc = EpeverS_CRC16(buf, len);
    buf[len]     = (uint8_t)(crc & 0xFF);   /* low byte first */
    buf[len + 1] = (uint8_t)(crc >> 8);
}

static EpeverS_Status_t read_regs(EpeverS_t *h, uint8_t fc, uint16_t addr,
                                  uint16_t qty, uint16_t *out)
{
    if (h == NULL || out == NULL || qty == 0 || qty > EPEVERS_MAX_REGS) {
        return EPEVERS_ERR_ARG;
    }

    uint8_t req[8];
    req[0] = h->slave_id;
    req[1] = fc;
    req[2] = (uint8_t)(addr >> 8);
    req[3] = (uint8_t)(addr & 0xFF);
    req[4] = (uint8_t)(qty >> 8);
    req[5] = (uint8_t)(qty & 0xFF);
    put_crc(req, 6);

    uint8_t rx[3 + 2 * EPEVERS_MAX_REGS + 2];
    uint16_t n;
    EpeverS_Status_t s = transact(h, req, sizeof(req), rx, sizeof(rx), &n);
    if (s != EPEVERS_OK) return s;

    if (rx[2] != qty * 2 || n != (uint16_t)(5 + qty * 2)) return EPEVERS_ERR_FRAME;

    for (uint16_t i = 0; i < qty; i++) {
        out[i] = (uint16_t)((rx[3 + i * 2] << 8) | rx[4 + i * 2]);
    }
    return EPEVERS_OK;
}

/* ------------------------------------------------------------------ */
/* Public generic API                                                  */
/* ------------------------------------------------------------------ */

void EpeverS_Init(EpeverS_t *h, UART_HandleTypeDef *huart,
                  GPIO_TypeDef *de_port, uint16_t de_pin,
                  GPIO_TypeDef *re_port, uint16_t re_pin,
                  uint8_t slave_id, uint32_t reply_timeout_ms)
{
    h->huart = huart;
    h->de_port = de_port;  h->de_pin = de_pin;
    h->re_port = re_port;  h->re_pin = re_pin;
    h->slave_id = slave_id;
    h->reply_timeout_ms = reply_timeout_ms;
    h->last_exception = 0;
    rx_mode(h);
}

EpeverS_Status_t EpeverS_ReadInputRegs(EpeverS_t *h, uint16_t addr, uint16_t qty, uint16_t *out)
{
    return read_regs(h, 0x04, addr, qty, out);
}

EpeverS_Status_t EpeverS_ReadHoldingRegs(EpeverS_t *h, uint16_t addr, uint16_t qty, uint16_t *out)
{
    return read_regs(h, 0x03, addr, qty, out);
}

EpeverS_Status_t EpeverS_WriteReg(EpeverS_t *h, uint16_t addr, uint16_t value)
{
    if (h == NULL) return EPEVERS_ERR_ARG;

    uint8_t req[8];
    req[0] = h->slave_id;
    req[1] = 0x06;
    req[2] = (uint8_t)(addr >> 8);
    req[3] = (uint8_t)(addr & 0xFF);
    req[4] = (uint8_t)(value >> 8);
    req[5] = (uint8_t)(value & 0xFF);
    put_crc(req, 6);

    uint8_t rx[8];
    uint16_t n;
    EpeverS_Status_t s = transact(h, req, sizeof(req), rx, sizeof(rx), &n);
    if (s != EPEVERS_OK) return s;

    /* The reply must echo the request. */
    if (n != 8 || memcmp(req, rx, 6) != 0) return EPEVERS_ERR_FRAME;
    return EPEVERS_OK;
}

/* ------------------------------------------------------------------ */
/* EPEVER real-time data                                               */
/* ------------------------------------------------------------------ */

/* 32-bit values: low word first (e.g. 0x3102 = low, 0x3103 = high). */
static float u32_div100(uint16_t lo, uint16_t hi)
{
    return (float)(((uint32_t)hi << 16) | lo) / 100.0f;
}

EpeverS_Status_t EpeverS_ReadRealtime(EpeverS_t *h, EpeverS_Realtime_t *out)
{
    if (out == NULL) return EPEVERS_ERR_ARG;

    uint16_t r[8];
    EpeverS_Status_t s;

    /* 0x3100..0x3107: PV and battery */
    s = EpeverS_ReadInputRegs(h, 0x3100, 8, r);
    if (s != EPEVERS_OK) return s;
    out->pv_v      = r[0] / 100.0f;
    out->pv_a      = r[1] / 100.0f;
    out->pv_w      = u32_div100(r[2], r[3]);
    out->battery_v = r[4] / 100.0f;
    out->battery_a = (int16_t)r[5] / 100.0f;
    out->battery_w = u32_div100(r[6], r[7]);

    /* 0x310C..0x310F: load */
    s = EpeverS_ReadInputRegs(h, 0x310C, 4, r);
    if (s != EPEVERS_OK) return s;
    out->load_v = r[0] / 100.0f;
    out->load_a = r[1] / 100.0f;
    out->load_w = u32_div100(r[2], r[3]);

    /* 0x3110..0x3111: temperatures */
    s = EpeverS_ReadInputRegs(h, 0x3110, 2, r);
    if (s != EPEVERS_OK) return s;
    out->battery_temp_c    = (int16_t)r[0] / 100.0f;
    out->controller_temp_c = (int16_t)r[1] / 100.0f;

    /* 0x311A: state of charge */
    s = EpeverS_ReadInputRegs(h, 0x311A, 1, r);
    if (s != EPEVERS_OK) return s;
    out->battery_soc = r[0];

    return EPEVERS_OK;
}

EpeverS_Status_t EpeverS_GetBatteryVoltage(EpeverS_t *h, float *volts)
{
    uint16_t r;
    EpeverS_Status_t s = EpeverS_ReadInputRegs(h, 0x3104, 1, &r);
    if (s == EPEVERS_OK && volts) *volts = r / 100.0f;
    return s;
}

EpeverS_Status_t EpeverS_GetPvVoltage(EpeverS_t *h, float *volts)
{
    uint16_t r;
    EpeverS_Status_t s = EpeverS_ReadInputRegs(h, 0x3100, 1, &r);
    if (s == EPEVERS_OK && volts) *volts = r / 100.0f;
    return s;
}

EpeverS_Status_t EpeverS_GetPvCurrent(EpeverS_t *h, float *current)
{
    uint16_t r;
    EpeverS_Status_t s = EpeverS_ReadInputRegs(h, 0x3101, 1, &r);
    if (s == EPEVERS_OK && current) *current = r / 100.0f;
    return s;
}
