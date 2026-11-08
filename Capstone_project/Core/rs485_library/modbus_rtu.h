/**
 * @file    modbus_rtu.h
 * @brief   Minimal blocking Modbus RTU master, built on top of the rs485.h
 *          transceiver driver. Not tied to EPEVER in any way -- usable for
 *          any Modbus RTU slave device.
 *
 * Supports the function codes needed by most simple instruments:
 *   - 0x02 Read Discrete Inputs
 *   - 0x03 Read Holding Registers
 *   - 0x04 Read Input Registers
 *   - 0x06 Write Single Register
 *   - 0x10 Write Multiple Registers
 *
 * All calls are blocking and use the underlying rs485.h driver's blocking
 * RS485_Transmit()/RS485_Receive(), so they're safe to call directly from
 * a bare-metal main loop. Not interrupt-safe / not reentrant for a single
 * RS485_Handle_t -- serialize your Modbus transactions (e.g. from one
 * task, or behind a mutex in an RTOS).
 */

#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include "rs485.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum registers/coils per single transaction this library supports.
 *  32 registers is generous for typical instrument register maps; raise it
 *  if you need bulk block reads, at the cost of larger stack buffers. */
#define MODBUS_MAX_REGISTERS 32

typedef enum {
    MODBUS_OK = 0,
    MODBUS_ERR_TX,              /**< underlying RS485/UART transmit failed        */
    MODBUS_ERR_RX_TIMEOUT,      /**< no/incomplete response within timeout_ms     */
    MODBUS_ERR_CRC,             /**< response CRC mismatch (line noise, etc.)     */
    MODBUS_ERR_BAD_RESPONSE,    /**< response address/function/length don't match request */
    MODBUS_ERR_EXCEPTION,       /**< slave returned a Modbus exception -- see Modbus_LastExceptionCode */
    MODBUS_ERR_BUFFER_TOO_SMALL /**< quantity requested exceeds MODBUS_MAX_REGISTERS or caller's buffer */
} Modbus_Status_t;

/** Set by any call that returns MODBUS_ERR_EXCEPTION. Standard Modbus
 *  exception codes: 1=ILLEGAL_FUNCTION, 2=ILLEGAL_DATA_ADDRESS,
 *  3=ILLEGAL_DATA_VALUE, 4=SLAVE_DEVICE_FAILURE, 6=SLAVE_DEVICE_BUSY. */
extern uint8_t Modbus_LastExceptionCode;

/** Read `quantity` (1..MODBUS_MAX_REGISTERS) holding registers (FC 0x03)
 *  starting at `start_addr` into `out_values` (big-endian register values,
 *  i.e. the natural 16-bit value of each register -- no scaling applied). */
Modbus_Status_t Modbus_ReadHoldingRegisters(RS485_Handle_t *rs485, uint8_t slave_addr,
                                             uint16_t start_addr, uint16_t quantity,
                                             uint16_t *out_values, uint32_t timeout_ms);

/** Same as above but function code 0x04 (Read Input Registers). */
Modbus_Status_t Modbus_ReadInputRegisters(RS485_Handle_t *rs485, uint8_t slave_addr,
                                           uint16_t start_addr, uint16_t quantity,
                                           uint16_t *out_values, uint32_t timeout_ms);

/** Read `quantity` (1..MODBUS_MAX_REGISTERS*8) discrete inputs (FC 0x02)
 *  starting at `start_addr`. `out_bits` must have room for `quantity`
 *  bytes; each is written as 0 or 1. */
Modbus_Status_t Modbus_ReadDiscreteInputs(RS485_Handle_t *rs485, uint8_t slave_addr,
                                           uint16_t start_addr, uint16_t quantity,
                                           uint8_t *out_bits, uint32_t timeout_ms);

/** Write a single holding register (FC 0x06). */
Modbus_Status_t Modbus_WriteSingleRegister(RS485_Handle_t *rs485, uint8_t slave_addr,
                                            uint16_t addr, uint16_t value, uint32_t timeout_ms);

/** Write `quantity` (1..MODBUS_MAX_REGISTERS) consecutive holding registers
 *  starting at `start_addr` (FC 0x10). */
Modbus_Status_t Modbus_WriteMultipleRegisters(RS485_Handle_t *rs485, uint8_t slave_addr,
                                               uint16_t start_addr, const uint16_t *values,
                                               uint16_t quantity, uint32_t timeout_ms);

/** Standard Modbus CRC-16 (polynomial 0xA001, init 0xFFFF), exposed in case
 *  you need it for a function code this library doesn't wrap. */
uint16_t Modbus_CRC16(const uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_H */