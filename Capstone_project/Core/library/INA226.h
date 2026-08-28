/*
 * INA226.h
 *
 *  Created on: Aug 21, 2026
 *      Author: Asus
 */

#ifndef INC_INA226_H_
#define INC_INA226_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
#include "stdbool.h"
#include "main.h"

extern volatile bool INA226_data_ready;
#define INA226_TIMEOUT 300

#define INA226_ADDR   (0x40 << 1)  /* A1=GND, A0=GND */

#define INA226_REG_CONFIG        0x00
#define INA226_REG_SHUNT_VOLTAGE 0x01
#define INA226_REG_BUS_VOLTAGE   0x02
#define INA226_REG_POWER         0x03
#define INA226_REG_CURRENT       0x04
#define INA226_REG_CALIBRATION   0x05
#define INA226_REG_MASK_ENABLE   0x06
#define INA226_REG_ALERT_LIMIT   0x07
#define INA226_REG_MANUFACTURER_ID 0xFE
#define INA226_REG_DIE_ID          0xFF

#define INA226_CONFIG_RESET        (1u << 15)

typedef enum {
    INA226_AVG_1    = 0x0,
    INA226_AVG_4    = 0x1,
    INA226_AVG_16   = 0x2,
    INA226_AVG_64   = 0x3,
    INA226_AVG_128  = 0x4,
    INA226_AVG_256  = 0x5,
    INA226_AVG_512  = 0x6,
    INA226_AVG_1024 = 0x7
} INA226_Avg_t;

typedef enum {
    INA226_VCT_140US  = 0x0,
    INA226_VCT_204US  = 0x1,
    INA226_VCT_332US  = 0x2,
    INA226_VCT_588US  = 0x3,
    INA226_VCT_1100US = 0x4, /* default */
    INA226_VCT_2116US = 0x5,
    INA226_VCT_4156US = 0x6,
    INA226_VCT_8244US = 0x7
} INA226_ConvTime_t;

typedef enum {
    INA226_MODE_POWER_DOWN        = 0x0,
    INA226_MODE_SHUNT_TRIG        = 0x1,
    INA226_MODE_BUS_TRIG          = 0x2,
    INA226_MODE_SHUNT_BUS_TRIG    = 0x3,
    INA226_MODE_POWER_DOWN2       = 0x4,
    INA226_MODE_SHUNT_CONT        = 0x5,
    INA226_MODE_BUS_CONT          = 0x6,
    INA226_MODE_SHUNT_BUS_CONT    = 0x7  /* default: continuous, both */
} INA226_Mode_t;

/* MASK/ENABLE (0x06) register (for alert pin) */
#define INA226_MASK_SOL   (1u << 15) /* Shunt Overvoltage */
#define INA226_MASK_SUL   (1u << 14) /* Shunt Undervoltage */
#define INA226_MASK_BOL   (1u << 13) /* Bus Overvoltage */
#define INA226_MASK_BUL   (1u << 12) /* Bus Undervoltage */
#define INA226_MASK_POL   (1u << 11) /* Power Over-limit */
#define INA226_MASK_CNVR  (1u << 10) /* Conversion Ready */
#define INA226_MASK_AFF   (1u << 4)  /* Alert Function Flag (read-only) */
#define INA226_MASK_CVRF  (1u << 3)  /* Conversion Ready Flag (read-only) */
#define INA226_MASK_OVF   (1u << 2)  /* Math Overflow Flag (read-only) */
#define INA226_MASK_APOL  (1u << 1)  /* Alert Polarity */
#define INA226_MASK_LEN   (1u << 0)  /* Alert Latch Enable */

typedef enum {
    INA226_OK = 0,
    INA226_ERROR_I2C,
    INA226_ERROR_INVALID_PARAM,
    INA226_ERROR_NOT_FOUND
} INA226_Status_t;

typedef struct {
    I2C_HandleTypeDef   *hi2c;
    uint16_t            dev_addr;
    float               shunt_ohms;
    float               current_lsb;
    float               power_lsb;
} INA226_HandleTypeDef;

/**
 * @brief Initialize the INA226: set config and set calibration value 
 *
 * @param shunt_ohms          normal number 
 * @param max_expected_amps   esti max charging current
 * @return true on success, false if the device is not responding
 */
INA226_Status_t INA226_Init(INA226_HandleTypeDef *dev,
                             I2C_HandleTypeDef *hi2c,
                             uint16_t i2c_addr_8bit,
                             float shunt_ohms,
                             float max_expected_amps);
 
/**
 * @brief Verify the device is present and responds with the expected
 *        manufacturer / die IDs.
 */
INA226_Status_t INA226_CheckDeviceID(INA226_HandleTypeDef *dev);
 
/** @brief Issue a soft reset (bit 15 of CONFIG). Re-init calibration after this. */
INA226_Status_t INA226_Reset(INA226_HandleTypeDef *dev);
 
/**
 * @brief Configure averaging, conversion times and operating mode.
 *        Calling this overwrites the whole CONFIG register (except that
 *        reserved bit 14 is forced to 1 as required by the datasheet).
 */
INA226_Status_t INA226_SetConfig(INA226_HandleTypeDef *dev,
                                  INA226_Avg_t avg,
                                  INA226_ConvTime_t bus_conv_time,
                                  INA226_ConvTime_t shunt_conv_time,
                                  INA226_Mode_t mode);
 
/** @brief Re-write the calibration register (rarely needed manually; INA226_Init does this). */
INA226_Status_t INA226_SetCalibration(INA226_HandleTypeDef *dev, float max_expected_amps);
 
/** @brief Read the bus voltage in volts (LSB = 1.25 mV). */
INA226_Status_t INA226_ReadBusVoltage(INA226_HandleTypeDef *dev, float *volts);
 
/** @brief Read the shunt voltage in volts (LSB = 2.5 uV). */
INA226_Status_t INA226_ReadShuntVoltage(INA226_HandleTypeDef *dev, float *volts);
 
/** @brief Read current in amps (requires calibration register to be set). */
INA226_Status_t INA226_ReadCurrent(INA226_HandleTypeDef *dev, float *amps);
 
/** @brief Read power in watts (requires calibration register to be set). */
INA226_Status_t INA226_ReadPower(INA226_HandleTypeDef *dev, float *watts);
 
/**
 * @brief Configure the MASK/ENABLE register to select which alert function
 *        drives the ALERT pin (e.g. INA226_MASK_BUL for bus undervoltage).
 */
INA226_Status_t INA226_SetAlertFunction(INA226_HandleTypeDef *dev, uint16_t mask_bits);
 
/**
 * @brief Set the alert limit register. Units depend on which alert function
 *        is active (voltage in 1.25mV/2.5uV steps, or power in power_lsb steps).
 */
INA226_Status_t INA226_SetAlertLimit(INA226_HandleTypeDef *dev, uint16_t raw_limit);
 
/** @brief Read the MASK/ENABLE register (also clears the alert flag when read). */
INA226_Status_t INA226_ReadMaskEnable(INA226_HandleTypeDef *dev, uint16_t *value);
 
/** @brief Returns true once a conversion has finished (polls CVRF flag). */
bool INA226_IsConversionReady(INA226_HandleTypeDef *dev);
 
/** @brief Low-level: read an arbitrary 16-bit register. */
INA226_Status_t INA226_ReadRegister(INA226_HandleTypeDef *dev, uint8_t reg, uint16_t *value);
 
/** @brief Low-level: write an arbitrary 16-bit register. */
INA226_Status_t INA226_WriteRegister(INA226_HandleTypeDef *dev, uint8_t reg, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* INC_INA226_H_ */
