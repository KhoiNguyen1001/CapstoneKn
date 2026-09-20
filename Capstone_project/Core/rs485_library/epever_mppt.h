/**
 * @file    epever_mppt.h
 * @brief   Typed register-map driver for EPEVER MPPT solar charge
 *          controllers (Tracer/Tracer AN/etc.) over Modbus RTU, built on
 *          modbus_rtu.h + rs485.h.
 *
 * Register addresses, scale factors, and status-bit layout are taken from
 * EPEVER's published Modbus protocol for these controllers (the same
 * protocol documented by the rosswarren/epevermodbus Python project,
 * MIT-licensed, https://github.com/rosswarren/epevermodbus). Only a subset
 * of the full register map is wrapped here -- the most commonly used
 * real-time data, status flags, and settings. Add more Epever_Get/Set
 * functions the same way if you need other registers.
 *
 * IMPORTANT -- verify on real hardware: the 32-bit values (power, energy
 * counters, battery current) use a "byte-swapped, word order preserved"
 * encoding across their two registers (documented in epever_mppt.c next
 * to decode_u32()). This has been worked out from the source library's
 * documented byte-order setting, not from an EPEVER datasheet directly --
 * double check a couple of readings (e.g. get_generated_energy_today
 * against the controller's own display, if it has one) the first time you
 * bring this up on your board.
 **/

#ifndef EPEVER_MPPT_H
#define EPEVER_MPPT_H

#include "modbus_rtu.h"
#include "rs485.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    RS485_Handle_t *rs485;
    uint8_t slave_address;  /**< 1-247 */
    uint32_t timeout_ms;    /**< per-transaction response timeout */
} Epever_Handle_t;

void Epever_Init(Epever_Handle_t *h, RS485_Handle_t *rs485, uint8_t slave_address, uint32_t timeout_ms);

/* ==================== Real-time data (0x31xx/0x33xx, FC 0x04) ========= */

Modbus_Status_t Epever_GetSolarVoltage(Epever_Handle_t *h, float *volts);
Modbus_Status_t Epever_GetSolarCurrent(Epever_Handle_t *h, float *amps);
Modbus_Status_t Epever_GetSolarPower(Epever_Handle_t *h, float *watts);

Modbus_Status_t Epever_GetLoadVoltage(Epever_Handle_t *h, float *volts);
Modbus_Status_t Epever_GetLoadCurrent(Epever_Handle_t *h, float *amps);
Modbus_Status_t Epever_GetLoadPower(Epever_Handle_t *h, float *watts);

Modbus_Status_t Epever_GetBatteryVoltage(Epever_Handle_t *h, float *volts);
Modbus_Status_t Epever_GetBatteryCurrent(Epever_Handle_t *h, float *amps); /* signed: negative = discharging */
Modbus_Status_t Epever_GetBatteryPower(Epever_Handle_t *h, float *watts);
Modbus_Status_t Epever_GetBatteryStateOfCharge(Epever_Handle_t *h, uint16_t *percent);
Modbus_Status_t Epever_GetBatteryTemperature(Epever_Handle_t *h, float *celsius);
Modbus_Status_t Epever_GetRemoteBatteryTemperature(Epever_Handle_t *h, float *celsius);
Modbus_Status_t Epever_GetControllerTemperature(Epever_Handle_t *h, float *celsius);
Modbus_Status_t Epever_GetBatteryRealRatedVoltage(Epever_Handle_t *h, float *volts);

Modbus_Status_t Epever_GetMaxPvVoltageToday(Epever_Handle_t *h, float *volts);
Modbus_Status_t Epever_GetMinPvVoltageToday(Epever_Handle_t *h, float *volts);
Modbus_Status_t Epever_GetMaxBatteryVoltageToday(Epever_Handle_t *h, float *volts);
Modbus_Status_t Epever_GetMinBatteryVoltageToday(Epever_Handle_t *h, float *volts);

Modbus_Status_t Epever_GetRatedChargingCurrent(Epever_Handle_t *h, float *amps);
Modbus_Status_t Epever_GetRatedLoadCurrent(Epever_Handle_t *h, float *amps);

/** All energy figures are in kWh. */
Modbus_Status_t Epever_GetConsumedEnergyToday(Epever_Handle_t *h, float *kwh);
Modbus_Status_t Epever_GetConsumedEnergyThisMonth(Epever_Handle_t *h, float *kwh);
Modbus_Status_t Epever_GetConsumedEnergyThisYear(Epever_Handle_t *h, float *kwh);
Modbus_Status_t Epever_GetTotalConsumedEnergy(Epever_Handle_t *h, float *kwh);
Modbus_Status_t Epever_GetGeneratedEnergyToday(Epever_Handle_t *h, float *kwh);
Modbus_Status_t Epever_GetGeneratedEnergyThisMonth(Epever_Handle_t *h, float *kwh);
Modbus_Status_t Epever_GetGeneratedEnergyThisYear(Epever_Handle_t *h, float *kwh);
Modbus_Status_t Epever_GetTotalGeneratedEnergy(Epever_Handle_t *h, float *kwh);

/* ==================== Status bitfields (0x32xx, FC 0x04) ============== */

typedef enum {
    EPEVER_BATTERY_STATUS_NORMAL         = 0,
    EPEVER_BATTERY_STATUS_OVER_VOLTAGE   = 1,
    EPEVER_BATTERY_STATUS_UNDER_VOLTAGE  = 2,
    EPEVER_BATTERY_STATUS_OVER_DISCHARGE = 3,
    EPEVER_BATTERY_STATUS_FAULT          = 4
} Epever_BatteryStatus_t;

typedef enum {
    EPEVER_TEMP_WARNING_NORMAL    = 0,
    EPEVER_TEMP_WARNING_OVER_TEMP = 1, /**< higher than warning setting */
    EPEVER_TEMP_WARNING_LOW_TEMP  = 2  /**< lower than warning setting  */
} Epever_TemperatureWarning_t;

typedef struct {
    bool wrong_identification_for_rated_voltage;
    bool battery_inner_resistance_abnormal;
    Epever_TemperatureWarning_t temperature_warning_status;
    Epever_BatteryStatus_t battery_status;
} Epever_BatteryStatusFlags_t;

Modbus_Status_t Epever_GetBatteryStatus(Epever_Handle_t *h, Epever_BatteryStatusFlags_t *out);

typedef enum {
    EPEVER_INPUT_VOLTAGE_NORMAL          = 0,
    EPEVER_INPUT_VOLTAGE_NO_INPUT_POWER  = 1,
    EPEVER_INPUT_VOLTAGE_HIGHER_INPUT    = 2,
    EPEVER_INPUT_VOLTAGE_ERROR           = 3
} Epever_ChargingInputVoltageStatus_t;

typedef enum {
    EPEVER_CHARGING_NONE          = 0,
    EPEVER_CHARGING_FLOAT         = 1,
    EPEVER_CHARGING_BOOST         = 2,
    EPEVER_CHARGING_EQUALIZATION  = 3
} Epever_ChargingStatus_t;

typedef struct {
    Epever_ChargingInputVoltageStatus_t input_voltage_status;
    bool charging_mosfet_short_circuit;
    bool charging_or_antireverse_mosfet_open_circuit;
    bool antireverse_mosfet_short_circuit;
    bool input_over_current;
    bool load_over_current;
    bool load_short_circuit;
    bool load_mosfet_short_circuit;
    bool disequilibrium_in_three_circuits;
    bool pv_input_short_circuit;
    Epever_ChargingStatus_t charging_status;
    bool fault;
    bool running;
} Epever_ChargingEquipmentStatus_t;

Modbus_Status_t Epever_GetChargingEquipmentStatus(Epever_Handle_t *h, Epever_ChargingEquipmentStatus_t *out);

typedef enum {
    EPEVER_DISCHG_INPUT_NORMAL    = 0,
    EPEVER_DISCHG_INPUT_LOW       = 1,
    EPEVER_DISCHG_INPUT_HIGH      = 2,
    EPEVER_DISCHG_INPUT_NO_ACCESS = 3
} Epever_DischargingInputStatus_t;

typedef enum {
    EPEVER_LOAD_LIGHT     = 0,
    EPEVER_LOAD_MODERATE  = 1,
    EPEVER_LOAD_RATED     = 2,
    EPEVER_LOAD_OVERLOAD  = 3
} Epever_OutputPowerLoad_t;

typedef struct {
    Epever_DischargingInputStatus_t input_voltage_status;
    Epever_OutputPowerLoad_t output_power_load;
    bool short_circuit;
    bool unable_to_discharge;
    bool unable_to_stop_discharging;
    bool output_voltage_abnormal;
    bool input_over_voltage;
    bool short_circuit_in_high_voltage_side;
    bool boost_over_voltage;
    bool output_over_voltage;
    bool fault;
    bool running;
} Epever_DischargingEquipmentStatus_t;

Modbus_Status_t Epever_GetDischargingEquipmentStatus(Epever_Handle_t *h, Epever_DischargingEquipmentStatus_t *out);

/* ==================== Discrete inputs (0x2xxx, FC 0x02) =============== */

Modbus_Status_t Epever_IsNight(Epever_Handle_t *h, bool *is_night);
Modbus_Status_t Epever_IsDay(Epever_Handle_t *h, bool *is_day);
Modbus_Status_t Epever_IsDeviceOverTemperature(Epever_Handle_t *h, bool *is_over_temp);

/* ==================== Settings (0x90xx, FC 0x03/0x06/0x10) ============ */

typedef enum {
    EPEVER_BATTERY_TYPE_USER_DEFINED = 0,
    EPEVER_BATTERY_TYPE_SEALED       = 1,
    EPEVER_BATTERY_TYPE_GEL          = 2,
    EPEVER_BATTERY_TYPE_FLOODED      = 3,
    EPEVER_BATTERY_TYPE_LIFEPO4      = 4, /**< raw register values 4-7 all mean this */
    EPEVER_BATTERY_TYPE_LI_NICOMN_O2 = 8  /**< raw register values 8-12 all mean this */
} Epever_BatteryType_t;

Modbus_Status_t Epever_GetBatteryType(Epever_Handle_t *h, Epever_BatteryType_t *type);

Modbus_Status_t Epever_GetBatteryCapacity(Epever_Handle_t *h, uint16_t *amp_hours);
Modbus_Status_t Epever_SetBatteryCapacity(Epever_Handle_t *h, uint16_t amp_hours);

Modbus_Status_t Epever_GetTemperatureCompensationCoefficient(Epever_Handle_t *h, float *coefficient);
Modbus_Status_t Epever_SetTemperatureCompensationCoefficient(Epever_Handle_t *h, float coefficient);

typedef enum {
    EPEVER_RATED_VOLTAGE_AUTO = 0,
    EPEVER_RATED_VOLTAGE_12V  = 1,
    EPEVER_RATED_VOLTAGE_24V  = 2,
    EPEVER_RATED_VOLTAGE_36V  = 3,
    EPEVER_RATED_VOLTAGE_48V  = 4,
    EPEVER_RATED_VOLTAGE_60V  = 5,
    EPEVER_RATED_VOLTAGE_110V = 6,
    EPEVER_RATED_VOLTAGE_120V = 7,
    EPEVER_RATED_VOLTAGE_220V = 8,
    EPEVER_RATED_VOLTAGE_240V = 9
} Epever_RatedVoltage_t;

Modbus_Status_t Epever_GetBatteryRatedVoltageSetting(Epever_Handle_t *h, Epever_RatedVoltage_t *setting);

typedef enum {
    EPEVER_CHARGING_MODE_VOLTAGE_COMPENSATION = 0,
    EPEVER_CHARGING_MODE_SOC                  = 1
} Epever_ChargingMode_t;

Modbus_Status_t Epever_GetChargingMode(Epever_Handle_t *h, Epever_ChargingMode_t *mode);
Modbus_Status_t Epever_GetDefaultLoadOnInManualMode(Epever_Handle_t *h, bool *load_on);
Modbus_Status_t Epever_GetEqualizeDurationMinutes(Epever_Handle_t *h, uint16_t *minutes);
Modbus_Status_t Epever_GetBoostDurationMinutes(Epever_Handle_t *h, uint16_t *minutes);
Modbus_Status_t Epever_GetBatteryDischargePercent(Epever_Handle_t *h, uint16_t *percent);
Modbus_Status_t Epever_GetBatteryChargePercent(Epever_Handle_t *h, uint16_t *percent);

/** The 12 battery voltage control points, in volts, in this fixed order
 *  (matches registers 0x9003..0x900E consecutively). */
typedef struct {
    float over_voltage_disconnect_voltage;
    float charging_limit_voltage;
    float over_voltage_reconnect_voltage;
    float equalize_charging_voltage;
    float boost_charging_voltage;
    float float_charging_voltage;
    float boost_reconnect_charging_voltage;
    float low_voltage_reconnect_voltage;
    float under_voltage_recover_voltage;
    float under_voltage_warning_voltage;
    float low_voltage_disconnect_voltage;
    float discharging_limit_voltage;
} Epever_BatteryVoltageControlPoints_t;

Modbus_Status_t Epever_GetBatteryVoltageControlPoints(Epever_Handle_t *h, Epever_BatteryVoltageControlPoints_t *out);

/** Writes all 12 points in a single multi-register write (the device
 *  expects them together). To change just one, call
 *  Epever_GetBatteryVoltageControlPoints() first, modify the field you
 *  want, then pass the whole struct back in here. */
Modbus_Status_t Epever_SetBatteryVoltageControlPoints(Epever_Handle_t *h, const Epever_BatteryVoltageControlPoints_t *values);

typedef struct {
    uint16_t year;  /**< full year, e.g. 2026 */
    uint8_t month;  /**< 1-12 */
    uint8_t day;    /**< 1-31 */
    uint8_t hour;   /**< 0-23 */
    uint8_t minute; /**< 0-59 */
    uint8_t second; /**< 0-59 */
} Epever_DateTime_t;

Modbus_Status_t Epever_GetRTC(Epever_Handle_t *h, Epever_DateTime_t *out);
Modbus_Status_t Epever_SetRTC(Epever_Handle_t *h, const Epever_DateTime_t *dt);

#ifdef __cplusplus
}
#endif

#endif /* EPEVER_MPPT_H */
