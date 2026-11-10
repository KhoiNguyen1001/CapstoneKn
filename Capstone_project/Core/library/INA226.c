/**
 ******************************************************************************
 * @file    ina226.c
 * @brief   Driver implementation for the TI INA226 current/power monitor.
 ******************************************************************************
 */

#include "ina226.h"

/* Fixed LSBs from the datasheet (do not change) */
#define INA226_BUS_VOLTAGE_LSB   0.00125f   /* 1.25 mV per bit  */
#define INA226_SHUNT_VOLTAGE_LSB 0.0000025f /* 2.5 uV per bit, not check, not used   */
#define INA226_POWER_LSB_MULT    25.0f      /* power_lsb = 25 * current_lsb */

#define INA226_MANUFACTURER_ID_EXPECTED 0x5449
#define INA226_DIE_ID_EXPECTED           0x2260

volatile bool INA226_data_ready = false;

INA226_Status_t INA226_ReadRegister(INA226_HandleTypeDef *dev, uint8_t reg, uint16_t *value)
{
    if (dev == NULL || value == NULL) {
        return INA226_ERROR_INVALID_PARAM;
    }

    uint8_t buf[2] = {0};
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(dev->hi2c, dev->dev_addr, reg,
                                                 I2C_MEMADD_SIZE_8BIT, buf, 2,
                                                 INA226_TIMEOUT);
    if (status != HAL_OK) {
        return INA226_ERROR_I2C;
    }

    *value = (uint16_t)((buf[0] << 8) | buf[1]);
    return INA226_OK;
}

INA226_Status_t INA226_WriteRegister(INA226_HandleTypeDef *dev, uint8_t reg, uint16_t value)
{
    if (dev == NULL) {
        return INA226_ERROR_INVALID_PARAM;
    }

    uint8_t buf[2];
    buf[0] = (uint8_t)((value >> 8) & 0xFF);
    buf[1] = (uint8_t)(value & 0xFF);

    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(dev->hi2c, dev->dev_addr, reg,
                                                  I2C_MEMADD_SIZE_8BIT, buf, 2,
                                                  INA226_TIMEOUT);
    if (status != HAL_OK) {
        return INA226_ERROR_I2C;
    }
    return INA226_OK;
}

INA226_Status_t INA226_CheckDeviceID(INA226_HandleTypeDef *dev)
{
    uint16_t mfg_id = 0, die_id = 0;
    INA226_Status_t status;

    status = INA226_ReadRegister(dev, INA226_REG_MANUFACTURER_ID, &mfg_id);
    if (status != INA226_OK) return status;

    status = INA226_ReadRegister(dev, INA226_REG_DIE_ID, &die_id);
    if (status != INA226_OK) return status;

    if (mfg_id != INA226_MANUFACTURER_ID_EXPECTED || die_id != INA226_DIE_ID_EXPECTED) {
        return INA226_ERROR_NOT_FOUND;
    }

    return INA226_OK;
}

INA226_Status_t INA226_Reset(INA226_HandleTypeDef *dev)
{
    return INA226_WriteRegister(dev, INA226_REG_CONFIG, INA226_CONFIG_RESET);
}

INA226_Status_t INA226_SetConfig(INA226_HandleTypeDef *dev,
                                  INA226_Avg_t avg,
                                  INA226_ConvTime_t bus_conv_time,
                                  INA226_ConvTime_t shunt_conv_time,
                                  INA226_Mode_t mode)
{
    if (dev == NULL) {
        return INA226_ERROR_INVALID_PARAM;
    }

    /* Bit 14 is reserved and must be written as 1 per datasheet. */
    uint16_t config = (1u << 14);
    config |= ((uint16_t)avg             & 0x7u) << 9;
    config |= ((uint16_t)bus_conv_time   & 0x7u) << 6;
    config |= ((uint16_t)shunt_conv_time & 0x7u) << 3;
    config |= ((uint16_t)mode            & 0x7u);

    return INA226_WriteRegister(dev, INA226_REG_CONFIG, config);
}

INA226_Status_t INA226_SetCalibration(INA226_HandleTypeDef *dev, float max_expected_amps)
{
    if (dev == NULL || dev->shunt_ohms <= 0.0f || max_expected_amps <= 0.0f) {
        return INA226_ERROR_INVALID_PARAM;
    }

    /* Per datasheet:
     *   current_lsb = max_expected_amps / 2^15
     *   CAL = 0.00512 / (current_lsb * shunt_ohms)
     * Choosing current_lsb this way maximizes resolution while
     * guaranteeing the ADC won't saturate before max_expected_amps. */
    float current_lsb = max_expected_amps / 32768.0f;
    float cal_f = 0.00512f / (current_lsb * dev->shunt_ohms);

    if (cal_f < 1.0f) {
        cal_f = 1.0f; /* clamp to a valid, non-zero register value */
    }
    if (cal_f > 65535.0f) {
        cal_f = 65535.0f;
    }

    uint16_t cal_reg = (uint16_t)(cal_f + 0.5f); /* round to nearest */

    INA226_Status_t status = INA226_WriteRegister(dev, INA226_REG_CALIBRATION, cal_reg);
    if (status != INA226_OK) {
        return status;
    }

    dev->current_lsb = current_lsb;
    dev->power_lsb = current_lsb * INA226_POWER_LSB_MULT;

    return INA226_OK;
}

INA226_Status_t INA226_Init(INA226_HandleTypeDef *dev,
                             I2C_HandleTypeDef *hi2c,
                             uint16_t i2c_addr_8bit,
                             float shunt_ohms,
                             float max_expected_amps)
{
    if (dev == NULL || hi2c == NULL || shunt_ohms <= 0.0f || max_expected_amps <= 0.0f) {
        return INA226_ERROR_INVALID_PARAM;
    }

    dev->hi2c = hi2c;
    dev->dev_addr = i2c_addr_8bit;
    dev->shunt_ohms = shunt_ohms;
    dev->current_lsb = 0.0f;
    dev->power_lsb = 0.0f;

    INA226_Status_t status = INA226_CheckDeviceID(dev);
    if (status != INA226_OK) {
        return status;
    }

    /* Sensible defaults: max averaging window off (avg=1), 1.1ms conversion
     * times, continuous shunt+bus measurement. Caller can override afterward
     * with INA226_SetConfig(). */
    status = INA226_SetConfig(dev, INA226_AVG_1,//INA226_AVG_16,
                            INA226_VCT_140US, INA226_VCT_8244US,
                            INA226_MODE_SHUNT_BUS_CONT);
    if (status != INA226_OK) {
        return status;
    }

    status = INA226_SetCalibration(dev, max_expected_amps);
    if (status != INA226_OK) {
        return status;
    }

    return INA226_OK;
}

INA226_Status_t INA226_ReadBusVoltage(INA226_HandleTypeDef *dev, float *volts)
{
    if (volts == NULL) return INA226_ERROR_INVALID_PARAM;

    uint16_t raw = 0;
    INA226_Status_t status = INA226_ReadRegister(dev, INA226_REG_BUS_VOLTAGE, &raw);
    if (status != INA226_OK) return status;

    *volts = (float)raw * INA226_BUS_VOLTAGE_LSB;
    return INA226_OK;
}

INA226_Status_t INA226_ReadShuntVoltage(INA226_HandleTypeDef *dev, float *volts)
{
    if (volts == NULL) return INA226_ERROR_INVALID_PARAM;

    uint16_t raw = 0;
    INA226_Status_t status = INA226_ReadRegister(dev, INA226_REG_SHUNT_VOLTAGE, &raw);
    if (status != INA226_OK) return status;

    /* SHUNT_VOLTAGE is a signed 16-bit two's complement value */
    int16_t signed_raw = (int16_t)raw;
    *volts = (float)signed_raw * INA226_SHUNT_VOLTAGE_LSB;
    return INA226_OK;
}

INA226_Status_t INA226_ReadCurrent(INA226_HandleTypeDef *dev, float *amps)
{
    if (dev == NULL || amps == NULL) return INA226_ERROR_INVALID_PARAM;
    if (dev->current_lsb <= 0.0f) return INA226_ERROR_INVALID_PARAM; /* not calibrated */

    uint16_t raw = 0;
    INA226_Status_t status = INA226_ReadRegister(dev, INA226_REG_CURRENT, &raw);
    if (status != INA226_OK) return status;

    /* CURRENT is a signed 16-bit two's complement value */
    int16_t signed_raw = (int16_t)raw;
    *amps = (float)signed_raw * dev->current_lsb;
    return INA226_OK;
}

INA226_Status_t INA226_ReadPower(INA226_HandleTypeDef *dev, float *watts)
{
    if (dev == NULL || watts == NULL) return INA226_ERROR_INVALID_PARAM;
    if (dev->power_lsb <= 0.0f) return INA226_ERROR_INVALID_PARAM; /* not calibrated */

    uint16_t raw = 0;
    INA226_Status_t status = INA226_ReadRegister(dev, INA226_REG_POWER, &raw);
    if (status != INA226_OK) return status;

    /* POWER is unsigned */
    *watts = (float)raw * dev->power_lsb;
    return INA226_OK;
}

INA226_Status_t INA226_SetAlertFunction(INA226_HandleTypeDef *dev, uint16_t mask_bits)
{
    return INA226_WriteRegister(dev, INA226_REG_MASK_ENABLE, mask_bits);
}

INA226_Status_t INA226_SetAlertLimit(INA226_HandleTypeDef *dev, uint16_t raw_limit)
{
    return INA226_WriteRegister(dev, INA226_REG_ALERT_LIMIT, raw_limit);
}

INA226_Status_t INA226_ReadMaskEnable(INA226_HandleTypeDef *dev, uint16_t *value)
{
    return INA226_ReadRegister(dev, INA226_REG_MASK_ENABLE, value);
}

bool INA226_IsConversionReady(INA226_HandleTypeDef *dev)
{
    uint16_t mask_enable = 0;
    if (INA226_ReadMaskEnable(dev, &mask_enable) != INA226_OK) {
        return false;
    }
    return (mask_enable & INA226_MASK_CVRF) != 0;
}