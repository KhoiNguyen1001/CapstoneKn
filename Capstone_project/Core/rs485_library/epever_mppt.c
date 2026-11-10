/**
 * @file epever_mppt.c
 * @brief See epever_mppt.h for documentation.
 */

#include "epever_mppt.h"

/* ==================== Register addresses ==============================
 * Real-time data & status live in input registers (FC 0x04), 0x3xxx range.
 * Settings live in holding registers (FC 0x03/0x06/0x10), 0x9xxx range,
 * plus a couple of rated-value registers at 0x3005/0x300E which are FC 4
 * despite the address prefix. Discrete status flags live at 0x2xxx (FC 2).
 * ======================================================================= */

#define REG_PV_VOLTAGE              0x3100
#define REG_PV_CURRENT              0x3101
#define REG_PV_POWER_L              0x3102 /* + 0x3103, 32-bit */
#define REG_BATTERY_POWER_L         0x3106 /* + 0x3107, 32-bit */
#define REG_LOAD_VOLTAGE            0x310C
#define REG_LOAD_CURRENT            0x310D
#define REG_LOAD_POWER_L            0x310E /* + 0x310F, 32-bit */
#define REG_BATTERY_TEMP            0x3110
#define REG_CONTROLLER_TEMP         0x3111
#define REG_BATTERY_SOC             0x311A
#define REG_REMOTE_BATTERY_TEMP     0x311B
#define REG_BATTERY_REAL_RATED_V    0x311D

#define REG_BATTERY_STATUS          0x3200
#define REG_CHARGING_EQUIP_STATUS   0x3201
#define REG_DISCHARGING_EQUIP_STATUS 0x3202

#define REG_MAX_PV_VOLTAGE_TODAY    0x3300
#define REG_MIN_PV_VOLTAGE_TODAY    0x3301
#define REG_MAX_BATT_VOLTAGE_TODAY  0x3302
#define REG_MIN_BATT_VOLTAGE_TODAY  0x3303
#define REG_CONSUMED_TODAY_L        0x3304 /* + next, 32-bit */
#define REG_CONSUMED_MONTH_L        0x3306
#define REG_CONSUMED_YEAR_L         0x3308
#define REG_CONSUMED_TOTAL_L        0x330A
#define REG_GENERATED_TODAY_L       0x330C
#define REG_GENERATED_MONTH_L       0x330E
#define REG_GENERATED_YEAR_L        0x3310
#define REG_GENERATED_TOTAL_L       0x3312
#define REG_BATTERY_VOLTAGE         0x331A
#define REG_BATTERY_CURRENT_L       0x331B /* + 0x331C, 32-bit, signed */

#define REG_RATED_CHARGING_CURRENT  0x3005
#define REG_RATED_LOAD_CURRENT      0x300E

#define REG_DI_OVER_TEMP            0x2000
#define REG_DI_IS_NIGHT             0x200C

#define REG_BATTERY_TYPE            0x9000
#define REG_BATTERY_CAPACITY        0x9001
#define REG_TEMP_COMPENSATION_COEF  0x9002
#define REG_VOLTAGE_POINTS_START    0x9003 /* 12 consecutive registers, 0x9003..0x900E */
#define REG_RATED_VOLTAGE_SETTING   0x9067
#define REG_DEFAULT_LOAD_ON_MANUAL  0x906A
#define REG_EQUALIZE_DURATION       0x906B
#define REG_BOOST_DURATION          0x906C
#define REG_BATTERY_DISCHARGE_PCT   0x906D
#define REG_BATTERY_CHARGE_PCT      0x906E
#define REG_CHARGING_MODE           0x9070
#define REG_RTC_SEC_MIN             0x9013
#define REG_RTC_HOUR_DAY            0x9014
#define REG_RTC_MONTH_YEAR          0x9015

/* ==================== small helpers ==================================== */

static inline uint16_t extract_bits(uint16_t value, uint8_t shift, uint16_t mask)
{
    return (uint16_t)((value >> shift) & mask);
}

/**
 * Reconstructs a 32-bit value from two consecutive registers. EPEVER stores
 * 32-bit quantities low word first: the register at the lower address (e.g.
 * 0x3102) is the LOW 16 bits and the next one (0x3103) is the HIGH 16 bits.
 * Each register is a normal big-endian Modbus register -- no byte swapping.
 * Equivalent to minimalmodbus's BYTEORDER_LITTLE_SWAP (verified against the
 * library itself: regs [0x2345, 0x0001] -> 74565).
 */
static uint32_t decode_u32(uint16_t reg_low, uint16_t reg_high)
{
    return ((uint32_t)reg_high << 16) | reg_low;
}

static Modbus_Status_t read_input_reg_raw(Epever_Handle_t *h, uint16_t addr, uint16_t *raw)
{
    return Modbus_ReadInputRegisters(h->rs485, h->slave_address, addr, 1, raw, h->timeout_ms);
}

static Modbus_Status_t read_holding_reg_raw(Epever_Handle_t *h, uint16_t addr, uint16_t *raw)
{
    return Modbus_ReadHoldingRegisters(h->rs485, h->slave_address, addr, 1, raw, h->timeout_ms);
}

static float scale_down(int32_t raw, uint8_t decimals)
{
    float divisor = 1.0f;
    for (uint8_t i = 0; i < decimals; i++) {
        divisor *= 10.0f;
    }
    return (float)raw / divisor;
}

static Modbus_Status_t read_scaled_input(Epever_Handle_t *h, uint16_t addr, uint8_t decimals, bool is_signed, float *out)
{
    uint16_t raw;
    Modbus_Status_t st = read_input_reg_raw(h, addr, &raw);
    if (st != MODBUS_OK) {
        return st;
    }
    *out = scale_down(is_signed ? (int16_t)raw : (int32_t)raw, decimals);
    return MODBUS_OK;
}

static Modbus_Status_t read_scaled_holding(Epever_Handle_t *h, uint16_t addr, uint8_t decimals, float *out)
{
    uint16_t raw;
    Modbus_Status_t st = read_holding_reg_raw(h, addr, &raw);
    if (st != MODBUS_OK) {
        return st;
    }
    *out = scale_down((int32_t)raw, decimals);
    return MODBUS_OK;
}

static Modbus_Status_t read_long_input(Epever_Handle_t *h, uint16_t addr, bool is_signed, uint8_t decimals, float *out)
{
    uint16_t regs[2];
    Modbus_Status_t st = Modbus_ReadInputRegisters(h->rs485, h->slave_address, addr, 2, regs, h->timeout_ms);
    if (st != MODBUS_OK) {
        return st;
    }
    uint32_t raw = decode_u32(regs[0], regs[1]);
    float value = is_signed ? (float)(int32_t)raw : (float)raw;
    for (uint8_t i = 0; i < decimals; i++) {
        value /= 10.0f;
    }
    *out = value;
    return MODBUS_OK;
}

static Modbus_Status_t read_u16_input(Epever_Handle_t *h, uint16_t addr, uint16_t *out)
{
    return read_input_reg_raw(h, addr, out);
}

static Modbus_Status_t read_u16_holding(Epever_Handle_t *h, uint16_t addr, uint16_t *out)
{
    return read_holding_reg_raw(h, addr, out);
}

_Static_assert(sizeof(Epever_BatteryVoltageControlPoints_t) == 12 * sizeof(float),
               "voltage control struct must be 12 packed floats (accessed as an array)");

/* ==================== public API ======================================== */

void Epever_Init(Epever_Handle_t *h, RS485_Handle_t *rs485, uint8_t slave_address, uint32_t timeout_ms)
{
    h->rs485 = rs485;
    h->slave_address = slave_address;
    h->timeout_ms = timeout_ms;
}

Modbus_Status_t Epever_ScanForSlaves(RS485_Handle_t *rs485, uint8_t first, uint8_t last,
                                     uint32_t timeout_ms, uint8_t *found, uint8_t max_found,
                                     uint8_t *count)
{
    if (rs485 == NULL || found == NULL || count == NULL ||
        first < 1 || last > 247 || first > last) {
        return MODBUS_ERR_BUFFER_TOO_SMALL;
    }
 
    *count = 0;
    for (uint16_t addr = first; addr <= last; addr++) {
        uint16_t dummy;
        Modbus_Status_t st = Modbus_ReadInputRegisters(rs485, (uint8_t)addr, REG_PV_VOLTAGE, 1, &dummy, timeout_ms);
        if (st == MODBUS_OK || st == MODBUS_ERR_EXCEPTION) {
            if (*count < max_found) {
                found[(*count)++] = (uint8_t)addr;
            } else {
                break; /* result array full */
            }
        }
    }
    return MODBUS_OK;
}

/* --- real-time data --- */

Modbus_Status_t Epever_GetSolarVoltage(Epever_Handle_t *h, float *volts)
{
    return read_scaled_input(h, REG_PV_VOLTAGE, 2, false, volts);
}

Modbus_Status_t Epever_GetSolarCurrent(Epever_Handle_t *h, float *amps)
{
    return read_scaled_input(h, REG_PV_CURRENT, 2, false, amps);
}

Modbus_Status_t Epever_GetSolarPower(Epever_Handle_t *h, float *watts)
{
    return read_long_input(h, REG_PV_POWER_L, false, 2, watts);
}

Modbus_Status_t Epever_GetLoadVoltage(Epever_Handle_t *h, float *volts)
{
    return read_scaled_input(h, REG_LOAD_VOLTAGE, 2, false, volts);
}

Modbus_Status_t Epever_GetLoadCurrent(Epever_Handle_t *h, float *amps)
{
    return read_scaled_input(h, REG_LOAD_CURRENT, 2, false, amps);
}

Modbus_Status_t Epever_GetLoadPower(Epever_Handle_t *h, float *watts)
{
    return read_long_input(h, REG_LOAD_POWER_L, false, 2, watts);
}

Modbus_Status_t Epever_GetBatteryVoltage(Epever_Handle_t *h, float *volts)
{
    return read_scaled_input(h, REG_BATTERY_VOLTAGE, 2, false, volts);
}

Modbus_Status_t Epever_GetBatteryCurrent(Epever_Handle_t *h, float *amps)
{
    return read_long_input(h, REG_BATTERY_CURRENT_L, true, 2, amps);
}

Modbus_Status_t Epever_GetBatteryPower(Epever_Handle_t *h, float *watts)
{
    return read_long_input(h, REG_BATTERY_POWER_L, false, 2, watts);
}

Modbus_Status_t Epever_GetBatteryStateOfCharge(Epever_Handle_t *h, uint16_t *percent)
{
    return read_u16_input(h, REG_BATTERY_SOC, percent);
}

Modbus_Status_t Epever_GetBatteryTemperature(Epever_Handle_t *h, float *celsius)
{
    return read_scaled_input(h, REG_BATTERY_TEMP, 2, true, celsius);
}

Modbus_Status_t Epever_GetRemoteBatteryTemperature(Epever_Handle_t *h, float *celsius)
{
    return read_scaled_input(h, REG_REMOTE_BATTERY_TEMP, 2, true, celsius);
}

Modbus_Status_t Epever_GetControllerTemperature(Epever_Handle_t *h, float *celsius)
{
    return read_scaled_input(h, REG_CONTROLLER_TEMP, 2, true, celsius);
}

Modbus_Status_t Epever_GetBatteryRealRatedVoltage(Epever_Handle_t *h, float *volts)
{
    return read_scaled_input(h, REG_BATTERY_REAL_RATED_V, 2, false, volts);
}

Modbus_Status_t Epever_GetMaxPvVoltageToday(Epever_Handle_t *h, float *volts)
{
    return read_scaled_input(h, REG_MAX_PV_VOLTAGE_TODAY, 2, false, volts);
}

Modbus_Status_t Epever_GetMinPvVoltageToday(Epever_Handle_t *h, float *volts)
{
    return read_scaled_input(h, REG_MIN_PV_VOLTAGE_TODAY, 2, false, volts);
}

Modbus_Status_t Epever_GetMaxBatteryVoltageToday(Epever_Handle_t *h, float *volts)
{
    return read_scaled_input(h, REG_MAX_BATT_VOLTAGE_TODAY, 2, false, volts);
}

Modbus_Status_t Epever_GetMinBatteryVoltageToday(Epever_Handle_t *h, float *volts)
{
    return read_scaled_input(h, REG_MIN_BATT_VOLTAGE_TODAY, 2, false, volts);
}

Modbus_Status_t Epever_GetRatedChargingCurrent(Epever_Handle_t *h, float *amps)
{
    return read_scaled_input(h, REG_RATED_CHARGING_CURRENT, 2, false, amps);
}

Modbus_Status_t Epever_GetRatedLoadCurrent(Epever_Handle_t *h, float *amps)
{
    return read_scaled_input(h, REG_RATED_LOAD_CURRENT, 2, false, amps);
}

Modbus_Status_t Epever_GetConsumedEnergyToday(Epever_Handle_t *h, float *kwh)
{
    return read_long_input(h, REG_CONSUMED_TODAY_L, false, 2, kwh);
}

Modbus_Status_t Epever_GetConsumedEnergyThisMonth(Epever_Handle_t *h, float *kwh)
{
    return read_long_input(h, REG_CONSUMED_MONTH_L, false, 2, kwh);
}

Modbus_Status_t Epever_GetConsumedEnergyThisYear(Epever_Handle_t *h, float *kwh)
{
    return read_long_input(h, REG_CONSUMED_YEAR_L, false, 2, kwh);
}

Modbus_Status_t Epever_GetTotalConsumedEnergy(Epever_Handle_t *h, float *kwh)
{
    return read_long_input(h, REG_CONSUMED_TOTAL_L, false, 2, kwh);
}

Modbus_Status_t Epever_GetGeneratedEnergyToday(Epever_Handle_t *h, float *kwh)
{
    return read_long_input(h, REG_GENERATED_TODAY_L, false, 2, kwh);
}

Modbus_Status_t Epever_GetGeneratedEnergyThisMonth(Epever_Handle_t *h, float *kwh)
{
    return read_long_input(h, REG_GENERATED_MONTH_L, false, 2, kwh);
}

Modbus_Status_t Epever_GetGeneratedEnergyThisYear(Epever_Handle_t *h, float *kwh)
{
    return read_long_input(h, REG_GENERATED_YEAR_L, false, 2, kwh);
}

Modbus_Status_t Epever_GetTotalGeneratedEnergy(Epever_Handle_t *h, float *kwh)
{
    return read_long_input(h, REG_GENERATED_TOTAL_L, false, 2, kwh);
}

/* --- status bitfields --- */

Modbus_Status_t Epever_GetBatteryStatus(Epever_Handle_t *h, Epever_BatteryStatusFlags_t *out)
{
    uint16_t raw;
    Modbus_Status_t st = read_input_reg_raw(h, REG_BATTERY_STATUS, &raw);
    if (st != MODBUS_OK) {
        return st;
    }
    out->wrong_identification_for_rated_voltage = extract_bits(raw, 15, 0x1) != 0;
    out->battery_inner_resistance_abnormal = extract_bits(raw, 8, 0x1) != 0;
    out->temperature_warning_status = (Epever_TemperatureWarning_t)extract_bits(raw, 4, 0x7);
    out->battery_status = (Epever_BatteryStatus_t)extract_bits(raw, 0, 0x7);
    return MODBUS_OK;
}

Modbus_Status_t Epever_GetChargingEquipmentStatus(Epever_Handle_t *h, Epever_ChargingEquipmentStatus_t *out)
{
    uint16_t raw;
    Modbus_Status_t st = read_input_reg_raw(h, REG_CHARGING_EQUIP_STATUS, &raw);
    if (st != MODBUS_OK) {
        return st;
    }
    out->input_voltage_status = (Epever_ChargingInputVoltageStatus_t)extract_bits(raw, 14, 0x3);
    out->charging_mosfet_short_circuit = extract_bits(raw, 13, 0x1) != 0;
    out->charging_or_antireverse_mosfet_open_circuit = extract_bits(raw, 12, 0x1) != 0;
    out->antireverse_mosfet_short_circuit = extract_bits(raw, 11, 0x1) != 0;
    out->input_over_current = extract_bits(raw, 10, 0x1) != 0;
    out->load_over_current = extract_bits(raw, 9, 0x1) != 0;
    out->load_short_circuit = extract_bits(raw, 8, 0x1) != 0;
    out->load_mosfet_short_circuit = extract_bits(raw, 7, 0x1) != 0;
    out->disequilibrium_in_three_circuits = extract_bits(raw, 6, 0x1) != 0;
    out->pv_input_short_circuit = extract_bits(raw, 4, 0x1) != 0;
    out->charging_status = (Epever_ChargingStatus_t)extract_bits(raw, 2, 0x3);
    /* Per the reference implementation this fault bit is unreliable on
     * real hardware (reports fault even when there isn't one) -- exposed
     * as-is; don't rely on it alone to detect faults. */
    out->fault = extract_bits(raw, 1, 0x1) != 0;
    out->running = extract_bits(raw, 0, 0x1) != 0;
    return MODBUS_OK;
}

Modbus_Status_t Epever_GetDischargingEquipmentStatus(Epever_Handle_t *h, Epever_DischargingEquipmentStatus_t *out)
{
    uint16_t raw;
    Modbus_Status_t st = read_input_reg_raw(h, REG_DISCHARGING_EQUIP_STATUS, &raw);
    if (st != MODBUS_OK) {
        return st;
    }
    out->input_voltage_status = (Epever_DischargingInputStatus_t)extract_bits(raw, 14, 0x3);
    out->output_power_load = (Epever_OutputPowerLoad_t)extract_bits(raw, 12, 0x3);
    out->short_circuit = extract_bits(raw, 11, 0x1) != 0;
    out->unable_to_discharge = extract_bits(raw, 10, 0x1) != 0;
    out->unable_to_stop_discharging = extract_bits(raw, 9, 0x1) != 0;
    out->output_voltage_abnormal = extract_bits(raw, 8, 0x1) != 0;
    out->input_over_voltage = extract_bits(raw, 7, 0x1) != 0;
    out->short_circuit_in_high_voltage_side = extract_bits(raw, 6, 0x1) != 0;
    out->boost_over_voltage = extract_bits(raw, 5, 0x1) != 0;
    out->output_over_voltage = extract_bits(raw, 4, 0x1) != 0;
    out->fault = extract_bits(raw, 1, 0x1) != 0;
    out->running = extract_bits(raw, 0, 0x1) != 0;
    return MODBUS_OK;
}

/* --- discrete inputs --- */

Modbus_Status_t Epever_IsNight(Epever_Handle_t *h, bool *is_night)
{
    uint8_t bit;
    Modbus_Status_t st = Modbus_ReadDiscreteInputs(h->rs485, h->slave_address, REG_DI_IS_NIGHT, 1, &bit, h->timeout_ms);
    if (st != MODBUS_OK) {
        return st;
    }
    *is_night = (bit != 0);
    return MODBUS_OK;
}

Modbus_Status_t Epever_IsDay(Epever_Handle_t *h, bool *is_day)
{
    bool night;
    Modbus_Status_t st = Epever_IsNight(h, &night);
    if (st != MODBUS_OK) {
        return st;
    }
    *is_day = !night;
    return MODBUS_OK;
}

Modbus_Status_t Epever_IsDeviceOverTemperature(Epever_Handle_t *h, bool *is_over_temp)
{
    uint8_t bit;
    Modbus_Status_t st = Modbus_ReadDiscreteInputs(h->rs485, h->slave_address, REG_DI_OVER_TEMP, 1, &bit, h->timeout_ms);
    if (st != MODBUS_OK) {
        return st;
    }
    *is_over_temp = (bit != 0);
    return MODBUS_OK;
}

/* --- settings --- */

Modbus_Status_t Epever_GetBatteryType(Epever_Handle_t *h, Epever_BatteryType_t *type)
{
    uint16_t raw;
    Modbus_Status_t st = read_holding_reg_raw(h, REG_BATTERY_TYPE, &raw);
    if (st != MODBUS_OK) {
        return st;
    }
    if (raw >= 8) {
        *type = EPEVER_BATTERY_TYPE_LI_NICOMN_O2; /* raw 8-12 */
    } else if (raw >= 4) {
        *type = EPEVER_BATTERY_TYPE_LIFEPO4; /* raw 4-7 */
    } else {
        *type = (Epever_BatteryType_t)raw; /* 0=USER_DEFINED, 1=SEALED, 2=GEL, 3=FLOODED */
    }
    return MODBUS_OK;
}

Modbus_Status_t Epever_GetBatteryCapacity(Epever_Handle_t *h, uint16_t *amp_hours)
{
    return read_u16_holding(h, REG_BATTERY_CAPACITY, amp_hours);
}

Modbus_Status_t Epever_SetBatteryCapacity(Epever_Handle_t *h, uint16_t amp_hours)
{
    return Modbus_WriteSingleRegister(h->rs485, h->slave_address, REG_BATTERY_CAPACITY, amp_hours, h->timeout_ms);
}

Modbus_Status_t Epever_GetTemperatureCompensationCoefficient(Epever_Handle_t *h, float *coefficient)
{
    return read_scaled_holding(h, REG_TEMP_COMPENSATION_COEF, 2, coefficient);
}

Modbus_Status_t Epever_SetTemperatureCompensationCoefficient(Epever_Handle_t *h, float coefficient)
{
    int16_t raw = (int16_t)(coefficient * 100.0f + (coefficient >= 0 ? 0.5f : -0.5f));
    return Modbus_WriteSingleRegister(h->rs485, h->slave_address, REG_TEMP_COMPENSATION_COEF, (uint16_t)raw, h->timeout_ms);
}

Modbus_Status_t Epever_GetBatteryRatedVoltageSetting(Epever_Handle_t *h, Epever_RatedVoltage_t *setting)
{
    uint16_t raw;
    Modbus_Status_t st = read_holding_reg_raw(h, REG_RATED_VOLTAGE_SETTING, &raw);
    if (st != MODBUS_OK) {
        return st;
    }
    *setting = (Epever_RatedVoltage_t)raw;
    return MODBUS_OK;
}

Modbus_Status_t Epever_GetChargingMode(Epever_Handle_t *h, Epever_ChargingMode_t *mode)
{
    uint16_t raw;
    Modbus_Status_t st = read_holding_reg_raw(h, REG_CHARGING_MODE, &raw);
    if (st != MODBUS_OK) {
        return st;
    }
    *mode = (Epever_ChargingMode_t)raw;
    return MODBUS_OK;
}

Modbus_Status_t Epever_GetDefaultLoadOnInManualMode(Epever_Handle_t *h, bool *load_on)
{
    uint16_t raw;
    Modbus_Status_t st = read_holding_reg_raw(h, REG_DEFAULT_LOAD_ON_MANUAL, &raw);
    if (st != MODBUS_OK) {
        return st;
    }
    *load_on = (raw != 0);
    return MODBUS_OK;
}

Modbus_Status_t Epever_GetEqualizeDurationMinutes(Epever_Handle_t *h, uint16_t *minutes)
{
    return read_u16_holding(h, REG_EQUALIZE_DURATION, minutes);
}

Modbus_Status_t Epever_GetBoostDurationMinutes(Epever_Handle_t *h, uint16_t *minutes)
{
    return read_u16_holding(h, REG_BOOST_DURATION, minutes);
}

Modbus_Status_t Epever_GetBatteryDischargePercent(Epever_Handle_t *h, uint16_t *percent)
{
    return read_u16_holding(h, REG_BATTERY_DISCHARGE_PCT, percent);
}

Modbus_Status_t Epever_GetBatteryChargePercent(Epever_Handle_t *h, uint16_t *percent)
{
    return read_u16_holding(h, REG_BATTERY_CHARGE_PCT, percent);
}

Modbus_Status_t Epever_GetBatteryVoltageControlPoints(Epever_Handle_t *h, Epever_BatteryVoltageControlPoints_t *out)
{
    uint16_t regs[12];
    Modbus_Status_t st = Modbus_ReadHoldingRegisters(h->rs485, h->slave_address, REG_VOLTAGE_POINTS_START, 12, regs, h->timeout_ms);
    if (st != MODBUS_OK) {
        return st;
    }
    float *fields = &out->over_voltage_disconnect_voltage; /* struct is 12 consecutive floats, same order as regs */
    for (int i = 0; i < 12; i++) {
        fields[i] = scale_down((int32_t)regs[i], 2);
    }
    return MODBUS_OK;
}

Modbus_Status_t Epever_SetBatteryVoltageControlPoints(Epever_Handle_t *h, const Epever_BatteryVoltageControlPoints_t *values)
{
    const float *fields = &values->over_voltage_disconnect_voltage;
    uint16_t regs[12];
    for (int i = 0; i < 12; i++) {
        regs[i] = (uint16_t)(fields[i] * 100.0f + 0.5f);
    }
    return Modbus_WriteMultipleRegisters(h->rs485, h->slave_address, REG_VOLTAGE_POINTS_START, regs, 12, h->timeout_ms);
}

Modbus_Status_t Epever_GetRTC(Epever_Handle_t *h, Epever_DateTime_t *out)
{
    uint16_t regs[3];
    Modbus_Status_t st = Modbus_ReadHoldingRegisters(h->rs485, h->slave_address, REG_RTC_SEC_MIN, 3, regs, h->timeout_ms);
    if (st != MODBUS_OK) {
        return st;
    }
    out->second = (uint8_t)extract_bits(regs[0], 0, 0xFF);
    out->minute = (uint8_t)extract_bits(regs[0], 8, 0xFF);
    out->hour   = (uint8_t)extract_bits(regs[1], 0, 0xFF);
    out->day    = (uint8_t)extract_bits(regs[1], 8, 0xFF);
    out->month  = (uint8_t)extract_bits(regs[2], 0, 0xFF);
    out->year   = (uint16_t)(extract_bits(regs[2], 8, 0xFF) + 2000);
    return MODBUS_OK;
}

Modbus_Status_t Epever_SetRTC(Epever_Handle_t *h, const Epever_DateTime_t *dt)
{
    uint16_t regs[3];
    regs[0] = (uint16_t)(dt->second | (dt->minute << 8));
    regs[1] = (uint16_t)(dt->hour | (dt->day << 8));
    regs[2] = (uint16_t)(dt->month | ((uint16_t)(dt->year - 2000) << 8));
    return Modbus_WriteMultipleRegisters(h->rs485, h->slave_address, REG_RTC_SEC_MIN, regs, 3, h->timeout_ms);
}