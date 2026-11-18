/**
 * @file    epever_simple.h
 * @brief   Small self-contained Modbus RTU + EPEVER MPPT reader for STM32 HAL
 *          and a MAX3491 (half-duplex, Y/A and Z/B strapped).
 *          One .c/.h pair, no other library needed. Based on the working
 *          modbus_simple_test.c.
 *
 * Usage:
 *   EpeverS_t mppt;
 *   EpeverS_Init(&mppt, &huart1,
 *                RS485_DE_GPIO_Port, RS485_DE_Pin,
 *                RS485_RE_GPIO_Port, RS485_RE_Pin,
 *                1, 200);
 *
 *   EpeverS_Realtime_t d;
 *   if (EpeverS_ReadRealtime(&mppt, &d) == EPEVERS_OK) {
 *       // d.battery_v, d.pv_w, ...
 *   }
 */

#ifndef EPEVER_SIMPLE_H
#define EPEVER_SIMPLE_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Silence before each request, in ms (Modbus wants 3.5 char times). */
#ifndef EPEVERS_INTERFRAME_MS
#define EPEVERS_INTERFRAME_MS 5
#endif

/** Max registers per request. */
#define EPEVERS_MAX_REGS 32

typedef enum {
    EPEVERS_OK = 0,
    EPEVERS_ERR_ARG,        /**< bad argument or quantity            */
    EPEVERS_ERR_TX,         /**< UART transmit failed                */
    EPEVERS_ERR_TIMEOUT,    /**< no reply                            */
    EPEVERS_ERR_FRAME,      /**< wrong address/function/length       */
    EPEVERS_ERR_CRC,        /**< CRC mismatch                        */
    EPEVERS_ERR_EXCEPTION   /**< slave sent Modbus exception         */
} EpeverS_Status_t;

typedef struct {
    UART_HandleTypeDef *huart;
    GPIO_TypeDef *de_port;  uint16_t de_pin;   /**< DE, active high */
    GPIO_TypeDef *re_port;  uint16_t re_pin;   /**< RE, active low  */
    uint8_t  slave_id;
    uint32_t reply_timeout_ms;                 /**< wait for first reply byte */
    uint8_t  last_exception;                   /**< set on EPEVERS_ERR_EXCEPTION */
} EpeverS_t;

/** Fill the handle and put the transceiver in receive mode. */
void EpeverS_Init(EpeverS_t *h, UART_HandleTypeDef *huart,
                  GPIO_TypeDef *de_port, uint16_t de_pin,
                  GPIO_TypeDef *re_port, uint16_t re_pin,
                  uint8_t slave_id, uint32_t reply_timeout_ms);

/* ---------- Generic Modbus (raw register values, no scaling) ---------- */

/** FC 0x04 read input registers. */
EpeverS_Status_t EpeverS_ReadInputRegs(EpeverS_t *h, uint16_t addr, uint16_t qty, uint16_t *out);

/** FC 0x03 read holding registers. */
EpeverS_Status_t EpeverS_ReadHoldingRegs(EpeverS_t *h, uint16_t addr, uint16_t qty, uint16_t *out);

/** FC 0x06 write single holding register. */
EpeverS_Status_t EpeverS_WriteReg(EpeverS_t *h, uint16_t addr, uint16_t value);

/** Standard Modbus CRC-16. */
uint16_t EpeverS_CRC16(const uint8_t *data, uint16_t len);

/* ---------- EPEVER real-time data ---------- */

typedef struct {
    float pv_v;          /**< V  (0x3100)           */
    float pv_a;          /**< A  (0x3101)           */
    float pv_w;          /**< W  (0x3102/3103)      */
    float battery_v;     /**< V  (0x3104)           */
    float battery_a;     /**< A, negative = discharging (0x3105) */
    float battery_w;     /**< W  (0x3106/3107)      */
    float load_v;        /**< V  (0x310C)           */
    float load_a;        /**< A  (0x310D)           */
    float load_w;        /**< W  (0x310E/310F)      */
    float battery_temp_c;     /**< degC (0x3110)    */
    float controller_temp_c;  /**< degC (0x3111)    */
    uint16_t battery_soc;     /**< %    (0x311A)    */
} EpeverS_Realtime_t;

/** Reads all real-time values above (4 Modbus requests). Stops at the first error. */
EpeverS_Status_t EpeverS_ReadRealtime(EpeverS_t *h, EpeverS_Realtime_t *out);

/** Convenience single reads. */
EpeverS_Status_t EpeverS_GetBatteryVoltage(EpeverS_t *h, float *volts);
EpeverS_Status_t EpeverS_GetPvVoltage(EpeverS_t *h, float *volts);
EpeverS_Status_t EpeverS_GetPvCurrent(EpeverS_t *h, float *current);

#ifdef __cplusplus
}
#endif

#endif /* EPEVER_SIMPLE_H */
