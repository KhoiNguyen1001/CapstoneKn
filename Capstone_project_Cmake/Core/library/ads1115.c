#include "ads1115.h"

volatile bool ads1115_data_ready = false;

bool ADS1115_StartConversion(ads1115_t *ads, ADS1115_Channel_t channel)
{
    ads->config &= ~ADS_REG_CONFIG_MUX_MASK;
    ads->config |= (uint16_t)channel;
    ads->config &= ~ADS_REG_CONFIG_MODE_MASK;
    ads->config |= ADS_REG_CONFIG_MODE_CONTINIOUS;   // single-shot mode
    ads->config |= ADS_REG_CONFIG_OS_MASK;       // OS=1 starts conversion

    ads1115_data_ready = false;

    return ads1115_write_register(ads, ADS_REG_CONFIG_ADDRESS, ads->config);
}

bool ADS1115_IsDataReady(void)
{
    return ads1115_data_ready;
}

bool ADS1115_ReadResult(ads1115_t *ads, float *raw)
{
    if (raw == NULL) {
        return false;
    }

    uint16_t result;
    if (!ads1115_read_register(ads, ADS_REG_CONVERSION_ADDRESS, &result)) {
        return false;
    }

    // FSR = ±2.048V over 15 bits (16-bit signed, one bit for sign)
    *raw = (float)(result * 2.048f / 32768.0f);

    ads1115_data_ready = false;
    return true;
}

bool ads1115_write_register(ads1115_t *ads, uint8_t reg, uint16_t value)
{
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (uint8_t)(value >> 8);
    buf[2] = (uint8_t)(value & 0xFF);

    return HAL_I2C_Master_Transmit(ads->i2c_handle, ads->address, buf, 3, ADS1xxx_TIMEOUT) == HAL_OK;
}

bool ads1115_read_register(ads1115_t *ads, uint8_t reg, uint16_t *value)
{
    uint8_t ptr = reg;
    uint8_t buf[2];

    if (HAL_I2C_Master_Transmit(ads->i2c_handle, ads->address, &ptr, 1, ADS1xxx_TIMEOUT) != HAL_OK) {
        return false;
    }
    if (HAL_I2C_Master_Receive(ads->i2c_handle, ads->address, buf, 2, ADS1xxx_TIMEOUT) != HAL_OK) {
        return false;
    }

    *value = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

bool ADS1115_SetGain(ads1115_t *ads, ads1115_fsr_t gain)
{
    ads->config &= ~ADS_REG_CONFIG_PGA_MASK;
    ads->config |= gain;
    return ads1115_write_register(ads, ADS_REG_CONFIG_ADDRESS, ads->config);
}

bool ADS1115_SetSPS(ads1115_t *ads, ads1115_sps_t sps)
{
    ads->config &= ~ADS_REG_CONFIG_DR_MASK;
    ads->config |= sps;
    return ads1115_write_register(ads, ADS_REG_CONFIG_ADDRESS, ads->config);
}

bool ADS1115_SetReadyPin(ads1115_t *ads)
{
    if (!ads1115_write_register(ads, ADS_REG_HITHRESH_ADRESS, 0x8000)) return false;
    if (!ads1115_write_register(ads, ADS_REG_LOTHRESH_ADRESS, 0x0000)) return false;

    ads->config &= ~ADS_REG_CONFIG_COMP_QUE_MASK;
    ads->config |= ADS_REG_CONFIG_COMP_QUE_1CONV;
    return ads1115_write_register(ads, ADS_REG_CONFIG_ADDRESS, ads->config);
}

bool ADS1115_Init(ads1115_t *ads, I2C_HandleTypeDef *i2c_handle, uint16_t addr, ads1115_fsr_t gain, ads1115_sps_t sps)
{
	ads->i2c_handle = i2c_handle;
	ads->address = addr;
	ads->gain = gain;
	ads->sps = sps;

    if (HAL_I2C_IsDeviceReady(ads->i2c_handle, ads->address, 3, 100) != HAL_OK) {
        return false;
    }

    ads->config = ADS_REG_CONFIG_RESET;
    ads->config &= ~ADS_REG_CONFIG_PGA_MASK;
    ads->config |= gain;
    ads->config &= ~ADS_REG_CONFIG_DR_MASK;
    ads->config |= sps;

    if (!ads1115_write_register(ads, ADS_REG_CONFIG_ADDRESS, ads->config)) {
        return false;
    }

    // Enable ALERT/RDY as conversion-ready pin
    return ADS1115_SetReadyPin(ads);
}
