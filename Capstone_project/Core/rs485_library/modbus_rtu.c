/**
 * @file modbus_rtu.c
 * @brief See modbus_rtu.h for documentation.
 */

#include "modbus_rtu.h"
#include <string.h>

uint8_t Modbus_LastExceptionCode = 0;

/** Silence enforced before each request (Modbus RTU wants >= 3.5 character
 *  times; 2 ms covers that up to 19200 baud and is plenty at 115200). Set to
 *  0 at build time (-DMODBUS_INTERFRAME_DELAY_MS=0) to disable. */
#ifndef MODBUS_INTERFRAME_DELAY_MS
#define MODBUS_INTERFRAME_DELAY_MS 2
#endif

uint16_t Modbus_CRC16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < length; pos++) {
        crc ^= (uint16_t)data[pos];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Send `req` (req_len bytes, CRC already appended) and read the
 *        response into `resp` (capacity resp_capacity), verifying address,
 *        function code, and CRC.
 *
 * If `fixed_response_len` is nonzero, the response is assumed to be that
 * exact length (used for the FC 0x06 / 0x10 write-ack responses, which
 * echo the request rather than carrying a byte-count field). Otherwise the
 * response is assumed to follow the standard "byte count" framing used by
 * FC 0x02/0x03/0x04 read responses: [addr][func][byte_count][data...][crc16].
 *
 * Transparently detects and decodes the 5-byte Modbus exception response
 * (addr, func|0x80, exception_code, crc16), returning MODBUS_ERR_EXCEPTION
 * with Modbus_LastExceptionCode set.
 */
static Modbus_Status_t modbus_transceive(RS485_Handle_t *rs485, uint8_t slave_addr, uint8_t func_code,
                                          const uint8_t *req, uint16_t req_len,
                                          uint8_t *resp, uint16_t resp_capacity, uint16_t *out_resp_len,
                                          uint16_t fixed_response_len, uint32_t timeout_ms)
{
#if MODBUS_INTERFRAME_DELAY_MS > 0
    HAL_Delay(MODBUS_INTERFRAME_DELAY_MS);
#endif
    RS485_FlushRx(rs485); /* drop stale bytes and clear overrun/framing flags */

    if (RS485_Transmit(rs485, req, req_len, timeout_ms) != HAL_OK) {
        return MODBUS_ERR_TX;
    }

    /* First 3 bytes are always: address, function code, and either a
     * byte-count (read responses), the high byte of the echoed address
     * (write responses), or the exception code (exception responses). */
    uint8_t hdr[3];
    if (RS485_Receive(rs485, hdr, 3, timeout_ms) != HAL_OK) {
        return MODBUS_ERR_RX_TIMEOUT;
    }

    if (hdr[0] != slave_addr) {
        return MODBUS_ERR_BAD_RESPONSE;
    }

    if (hdr[1] == (uint8_t)(func_code | 0x80)) {
        /* Exception frame: addr, func|0x80, exception_code, crc_lo, crc_hi */
        uint8_t tail[2];
        if (RS485_Receive(rs485, tail, 2, timeout_ms) != HAL_OK) {
            return MODBUS_ERR_RX_TIMEOUT;
        }
        uint8_t frame[5] = { hdr[0], hdr[1], hdr[2], tail[0], tail[1] };
        uint16_t crc_calc = Modbus_CRC16(frame, 3);
        uint16_t crc_recv = (uint16_t)tail[0] | ((uint16_t)tail[1] << 8);
        if (crc_calc != crc_recv) {
            return MODBUS_ERR_CRC;
        }
        Modbus_LastExceptionCode = hdr[2];
        return MODBUS_ERR_EXCEPTION;
    }

    if (hdr[1] != func_code) {
        return MODBUS_ERR_BAD_RESPONSE;
    }

    uint16_t total_len = fixed_response_len ? fixed_response_len : (uint16_t)(3 + hdr[2] + 2);

    if (total_len > resp_capacity || total_len < 3) {
        return MODBUS_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(resp, hdr, 3);
    if (total_len > 3) {
        if (RS485_Receive(rs485, resp + 3, (uint16_t)(total_len - 3), timeout_ms) != HAL_OK) {
            return MODBUS_ERR_RX_TIMEOUT;
        }
    }

    uint16_t crc_calc = Modbus_CRC16(resp, (uint16_t)(total_len - 2));
    uint16_t crc_recv = (uint16_t)resp[total_len - 2] | ((uint16_t)resp[total_len - 1] << 8);
    if (crc_calc != crc_recv) {
        return MODBUS_ERR_CRC;
    }

    *out_resp_len = total_len;
    return MODBUS_OK;
}

static Modbus_Status_t modbus_read_registers(RS485_Handle_t *rs485, uint8_t slave_addr, uint8_t func_code,
                                              uint16_t start_addr, uint16_t quantity,
                                              uint16_t *out_values, uint32_t timeout_ms)
{
    if (quantity == 0 || quantity > MODBUS_MAX_REGISTERS) {
        return MODBUS_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t req[8];
    req[0] = slave_addr;
    req[1] = func_code;
    req[2] = (uint8_t)(start_addr >> 8);
    req[3] = (uint8_t)(start_addr & 0xFF);
    req[4] = (uint8_t)(quantity >> 8);
    req[5] = (uint8_t)(quantity & 0xFF);
    uint16_t crc = Modbus_CRC16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    uint8_t resp[3 + 2 * MODBUS_MAX_REGISTERS + 2];
    uint16_t resp_len;
    Modbus_Status_t st = modbus_transceive(rs485, slave_addr, func_code, req, sizeof(req),
                                            resp, sizeof(resp), &resp_len, 0, timeout_ms);
    if (st != MODBUS_OK) {
        return st;
    }

    uint8_t byte_count = resp[2];
    if (byte_count != quantity * 2) {
        return MODBUS_ERR_BAD_RESPONSE;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        out_values[i] = ((uint16_t)resp[3 + i * 2] << 8) | resp[3 + i * 2 + 1];
    }
    return MODBUS_OK;
}

Modbus_Status_t Modbus_ReadHoldingRegisters(RS485_Handle_t *rs485, uint8_t slave_addr,
                                             uint16_t start_addr, uint16_t quantity,
                                             uint16_t *out_values, uint32_t timeout_ms)
{
    return modbus_read_registers(rs485, slave_addr, 0x03, start_addr, quantity, out_values, timeout_ms);
}

Modbus_Status_t Modbus_ReadInputRegisters(RS485_Handle_t *rs485, uint8_t slave_addr,
                                           uint16_t start_addr, uint16_t quantity,
                                           uint16_t *out_values, uint32_t timeout_ms)
{
    return modbus_read_registers(rs485, slave_addr, 0x04, start_addr, quantity, out_values, timeout_ms);
}

Modbus_Status_t Modbus_ReadDiscreteInputs(RS485_Handle_t *rs485, uint8_t slave_addr,
                                           uint16_t start_addr, uint16_t quantity,
                                           uint8_t *out_bits, uint32_t timeout_ms)
{
    if (quantity == 0 || quantity > MODBUS_MAX_REGISTERS * 8) {
        return MODBUS_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t req[8];
    req[0] = slave_addr;
    req[1] = 0x02;
    req[2] = (uint8_t)(start_addr >> 8);
    req[3] = (uint8_t)(start_addr & 0xFF);
    req[4] = (uint8_t)(quantity >> 8);
    req[5] = (uint8_t)(quantity & 0xFF);
    uint16_t crc = Modbus_CRC16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    uint8_t expected_byte_count = (uint8_t)((quantity + 7) / 8);
    uint8_t resp[3 + MODBUS_MAX_REGISTERS + 2];
    uint16_t resp_len;
    Modbus_Status_t st = modbus_transceive(rs485, slave_addr, 0x02, req, sizeof(req),
                                            resp, sizeof(resp), &resp_len, 0, timeout_ms);
    if (st != MODBUS_OK) {
        return st;
    }

    if (resp[2] != expected_byte_count) {
        return MODBUS_ERR_BAD_RESPONSE;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        uint8_t byte = resp[3 + i / 8];
        out_bits[i] = (byte >> (i % 8)) & 0x01;
    }
    return MODBUS_OK;
}

Modbus_Status_t Modbus_WriteSingleRegister(RS485_Handle_t *rs485, uint8_t slave_addr,
                                            uint16_t addr, uint16_t value, uint32_t timeout_ms)
{
    uint8_t req[8];
    req[0] = slave_addr;
    req[1] = 0x06;
    req[2] = (uint8_t)(addr >> 8);
    req[3] = (uint8_t)(addr & 0xFF);
    req[4] = (uint8_t)(value >> 8);
    req[5] = (uint8_t)(value & 0xFF);
    uint16_t crc = Modbus_CRC16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    uint8_t resp[8];
    uint16_t resp_len;
    Modbus_Status_t st = modbus_transceive(rs485, slave_addr, 0x06, req, sizeof(req),
                                            resp, sizeof(resp), &resp_len, 8, timeout_ms);
    if (st != MODBUS_OK) {
        return st;
    }

    if (memcmp(req, resp, 6) != 0) {
        return MODBUS_ERR_BAD_RESPONSE; /* echoed addr/register/value should match exactly */
    }
    return MODBUS_OK;
}

Modbus_Status_t Modbus_WriteMultipleRegisters(RS485_Handle_t *rs485, uint8_t slave_addr,
                                               uint16_t start_addr, const uint16_t *values,
                                               uint16_t quantity, uint32_t timeout_ms)
{
    if (quantity == 0 || quantity > MODBUS_MAX_REGISTERS) {
        return MODBUS_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t byte_count = (uint8_t)(quantity * 2);
    uint8_t req[7 + 2 * MODBUS_MAX_REGISTERS + 2];

    req[0] = slave_addr;
    req[1] = 0x10;
    req[2] = (uint8_t)(start_addr >> 8);
    req[3] = (uint8_t)(start_addr & 0xFF);
    req[4] = (uint8_t)(quantity >> 8);
    req[5] = (uint8_t)(quantity & 0xFF);
    req[6] = byte_count;
    for (uint16_t i = 0; i < quantity; i++) {
        req[7 + i * 2] = (uint8_t)(values[i] >> 8);
        req[7 + i * 2 + 1] = (uint8_t)(values[i] & 0xFF);
    }
    uint16_t req_len = (uint16_t)(7 + byte_count);
    uint16_t crc = Modbus_CRC16(req, req_len);
    req[req_len] = (uint8_t)(crc & 0xFF);
    req[req_len + 1] = (uint8_t)(crc >> 8);
    req_len = (uint16_t)(req_len + 2);

    uint8_t resp[8];
    uint16_t resp_len;
    Modbus_Status_t st = modbus_transceive(rs485, slave_addr, 0x10, req, req_len,
                                            resp, sizeof(resp), &resp_len, 8, timeout_ms);
    if (st != MODBUS_OK) {
        return st;
    }

    /* Response echoes addr, func, start_addr, quantity (not the data). */
    if (memcmp(req, resp, 6) != 0) {
        return MODBUS_ERR_BAD_RESPONSE;
    }
    return MODBUS_OK;
}