/*
 *
 * Copyright 2020-present Facebook. All Rights Reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __PAL_SENSORS_H__
#define __PAL_SENSORS_H__
#include <openbmc/obmc-pal.h>
#include <openbmc/kv.h>
#include <openbmc/obmc_pal_sensors.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <openbmc/ipmi.h>
#include <stdbool.h>
#include <math.h>
#include <facebook/bic.h>

// SCM Sensor Devices
#define SCM_HSC_DEVICE                I2C_DEV_DIR(24, 10)HW_MON_DIR
#define SCM_OUTLET_TEMP_DEVICE        I2C_DEV_DIR(25, 4d)HW_MON_DIR
#define SCM_INLET_TEMP_DEVICE         I2C_DEV_DIR(25, 4c)HW_MON_DIR

// SMB Sensor Devices
#define SMB_PXE1211_DEVICE            I2C_DEV_DIR(20, 0e)HW_MON_DIR
#define SMB_VDDA_DEVICE               I2C_DEV_DIR(16, 4d)HW_MON_DIR
#define SMB_PCIE_DEVICE               I2C_DEV_DIR(21, 47)HW_MON_DIR
#define SMB_1220_DEVICE               I2C_DEV_DIR(18, 3a)HW_MON_DIR
#define SMB_XPDE_DEVICE               I2C_DEV_DIR(17, 40)HW_MON_DIR
#define SMB_XPDE_LEFT_DEVICE          I2C_DEV_DIR(19, 68)HW_MON_DIR
#define SMB_XPDE_RIGHT_DEVICE         I2C_DEV_DIR(22, 60)HW_MON_DIR
#define SMB_LM75B_U28_DEVICE          I2C_DEV_DIR(32, 48)HW_MON_DIR
#define SMB_LM75B_U25_DEVICE          I2C_DEV_DIR(33, 49)HW_MON_DIR
#define SMB_LM75B_U56_DEVICE          I2C_DEV_DIR(3, 4a)HW_MON_DIR
#define SMB_LM75B_U55_DEVICE          I2C_DEV_DIR(34, 4b)HW_MON_DIR
#define SMB_LM75B_U2_DEVICE           I2C_DEV_DIR(37, 4e)HW_MON_DIR
#define SMB_LM75B_U13_DEVICE          I2C_DEV_DIR(39, 4f)HW_MON_DIR
#define SMB_TMP421_U62_DEVICE         I2C_DEV_DIR(35, 4c)HW_MON_DIR
#define SMB_TMP421_U63_DEVICE         I2C_DEV_DIR(36, 4d)HW_MON_DIR
#define SMB_LM75B_BMC_DEVICE          I2C_DEV_DIR(8, 4a)HW_MON_DIR
#define SMB_GB_TEMP_DEVICE            I2C_DEV_DIR(38, 2a)HW_MON_DIR
#define SMB_DOM1_DEVICE               I2C_DEV_DIR(13, 60)
#define SMB_DOM2_DEVICE               I2C_DEV_DIR(5, 60)
#define SMB_FCM_TACH_DEVICE           I2C_DEV_DIR(48, 3e)
#define SMB_FCM_LM75B_U1_DEVICE       I2C_DEV_DIR(50, 48)HW_MON_DIR
#define SMB_FCM_LM75B_U2_DEVICE       I2C_DEV_DIR(50, 49)HW_MON_DIR
#define SMB_FCM_HSC_DEVICE            I2C_DEV_DIR(51, 10)HW_MON_DIR

//BMC Sensor Devicesxx
#define AST_ADC_DEVICE                SYS_PLATFROM_DIR("ast-adc-hwmon")HW_MON_DIR

// PSU Sensor Devices
#define PSU_DRIVER                    "psu_driver"
#define PSU1_DEVICE                   I2C_DEV_DIR(40, 58)
#define PSU2_DEVICE                   I2C_DEV_DIR(41, 58)

#define TEMP(x)                       "temp"#x"_input"
#define VOLT(x)                       "in"#x"_input"
#define VOLT_SET(x)                   "vo"#x"_input"
#define CURR(x)                       "curr"#x"_input"
#define POWER(x)                      "power"#x"_input"

#define UNIT_DIV 1000

#define READING_SKIP 1
#define READING_NA -2

#define THRESHOLD_NA 0

#define SCM_RSENSE 1

#define READ_UNIT_SENSOR_TIMEOUT 5
#define MAX_SENSOR_NUM  0xFF
#define MAX_SENSOR_THRESHOLD  8
#define MAX_SDR_LEN     64

/* Sensors on SCM */
enum {
  SCM_SENSOR_OUTLET_TEMP = 0x02,
  SCM_SENSOR_INLET_TEMP = 0x04,
  SCM_SENSOR_HSC_IN_VOLT = 0x0A,
  SCM_SENSOR_HSC_OUT_VOLT = 0x0C,
  SCM_SENSOR_HSC_OUT_CURR = 0x0D,
  /* Threshold Sensors on COM-e */
  BIC_SENSOR_MB_OUTLET_TEMP = 0x01,
  BIC_SENSOR_MB_INLET_TEMP = 0x07,
  BIC_SENSOR_PCH_TEMP = 0x08,
  BIC_SENSOR_VCCIN_VR_TEMP = 0x80,
  BIC_SENSOR_1V05COMB_VR_TEMP = 0x81,
  BIC_SENSOR_SOC_TEMP = 0x05,
  BIC_SENSOR_SOC_THERM_MARGIN = 0x09,
  BIC_SENSOR_VDDR_VR_TEMP = 0x82,
  BIC_SENSOR_SOC_DIMMA_TEMP = 0xB4,
  BIC_SENSOR_SOC_DIMMB_TEMP = 0xB6,
  BIC_SENSOR_SOC_PACKAGE_PWR = 0x2C,
  BIC_SENSOR_VCCIN_VR_POUT = 0x8B,
  BIC_SENSOR_VDDR_VR_POUT = 0x8D,
  BIC_SENSOR_SOC_TJMAX = 0x30,
  BIC_SENSOR_P3V3_MB = 0xD0,
  BIC_SENSOR_P12V_MB = 0xD2,
  BIC_SENSOR_P1V05_PCH = 0xD3,
  BIC_SENSOR_P3V3_STBY_MB = 0xD5,
  BIC_SENSOR_P5V_STBY_MB = 0xD6,
  BIC_SENSOR_PV_BAT = 0xD7,
  BIC_SENSOR_PVDDR = 0xD8,
  BIC_SENSOR_P1V05_COMB = 0x8E,
  BIC_SENSOR_1V05COMB_VR_CURR = 0x84,
  BIC_SENSOR_VDDR_VR_CURR = 0x85,
  BIC_SENSOR_VCCIN_VR_CURR = 0x83,
  BIC_SENSOR_VCCIN_VR_VOL = 0x88,
  BIC_SENSOR_VDDR_VR_VOL = 0x8A,
  BIC_SENSOR_P1V05COMB_VR_VOL = 0x89,
  BIC_SENSOR_P1V05COMB_VR_POUT = 0x8C,
  BIC_SENSOR_INA230_POWER = 0x29,
  BIC_SENSOR_INA230_VOL = 0x2A,
  /* Discrete sensors on COM-e*/
  BIC_SENSOR_SYSTEM_STATUS = 0x10, //Discrete
  BIC_SENSOR_PROC_FAIL = 0x65, //Discrete
  BIC_SENSOR_SYS_BOOT_STAT = 0x7E, //Discrete
  BIC_SENSOR_VR_HOT = 0xB2, //Discrete
  BIC_SENSOR_CPU_DIMM_HOT = 0xB3, //Discrete None in Spec
  /* Event-only sensors on COM-e */
  BIC_SENSOR_SPS_FW_HLTH = 0x17, //Event-only
  BIC_SENSOR_POST_ERR = 0x2B, //Event-only
  BIC_SENSOR_POWER_THRESH_EVENT = 0x3B, //Event-only
  BIC_SENSOR_MACHINE_CHK_ERR = 0x40, //Event-only
  BIC_SENSOR_PCIE_ERR = 0x41, //Event-only
  BIC_SENSOR_OTHER_IIO_ERR = 0x43, //Event-only
  BIC_SENSOR_PROC_HOT_EXT = 0x51, //Event-only None in Spec
  BIC_SENSOR_POWER_ERR = 0x56, //Event-only
  BIC_SENSOR_MEM_ECC_ERR = 0x63, //Event-only
  BIC_SENSOR_CAT_ERR = 0xEB, //Event-only
};

/* Sensors on SMB */
enum {
  SMB_SENSOR_1220_VMON1 = 0x01,
  SMB_SENSOR_1220_VMON2,
  SMB_SENSOR_1220_VMON3,
  SMB_SENSOR_1220_VMON4,
  SMB_SENSOR_1220_VMON5,
  SMB_SENSOR_1220_VMON6,
  SMB_SENSOR_1220_VMON7,
  SMB_SENSOR_1220_VMON8,
  SMB_SENSOR_1220_VMON9,
  SMB_SENSOR_1220_VMON10,
  SMB_SENSOR_1220_VMON11,
  SMB_SENSOR_1220_VMON12,
  SMB_SENSOR_1220_VCCA,
  SMB_SENSOR_1220_VCCINP,
  SMB_SENSOR_VDDA_IN_VOLT,
  SMB_SENSOR_VDDA_IN_CURR,
  SMB_SENSOR_VDDA_IN_POWER,
  SMB_SENSOR_VDDA_OUT_VOLT,
  SMB_SENSOR_VDDA_OUT_CURR,
  SMB_SENSOR_VDDA_OUT_POWER,
  SMB_SENSOR_VDDA_TEMP1,
  SMB_SENSOR_PCIE_IN_VOLT,
  SMB_SENSOR_PCIE_IN_CURR,
  SMB_SENSOR_PCIE_IN_POWER,
  SMB_SENSOR_PCIE_OUT_VOLT,
  SMB_SENSOR_PCIE_OUT_CURR,
  SMB_SENSOR_PCIE_OUT_POWER,
  SMB_SENSOR_PCIE_TEMP1,
  SMB_SENSOR_IR3R3V_LEFT_IN_VOLT,
  SMB_SENSOR_IR3R3V_LEFT_IN_CURR,
  SMB_SENSOR_IR3R3V_LEFT_IN_POWER,
  SMB_SENSOR_IR3R3V_LEFT_OUT_VOLT,
  SMB_SENSOR_IR3R3V_LEFT_OUT_CURR,
  SMB_SENSOR_IR3R3V_LEFT_OUT_POWER,
  SMB_SENSOR_IR3R3V_LEFT_TEMP,
  SMB_SENSOR_IR3R3V_RIGHT_IN_VOLT,
  SMB_SENSOR_IR3R3V_RIGHT_IN_CURR,
  SMB_SENSOR_IR3R3V_RIGHT_IN_POWER,
  SMB_SENSOR_IR3R3V_RIGHT_OUT_VOLT,
  SMB_SENSOR_IR3R3V_RIGHT_OUT_CURR,
  SMB_SENSOR_IR3R3V_RIGHT_OUT_POWER,
  SMB_SENSOR_IR3R3V_RIGHT_TEMP,
  SMB_SENSOR_SW_CORE_IN_VOLT,
  SMB_SENSOR_SW_CORE_IN_CURR,
  SMB_SENSOR_SW_CORE_IN_POWER,
  SMB_SENSOR_SW_CORE_OUT_VOLT,
  SMB_SENSOR_SW_CORE_OUT_CURR,
  SMB_SENSOR_SW_CORE_OUT_POWER,
  SMB_SENSOR_SW_CORE_TEMP1,
  SMB_SENSOR_XPDE_HBM_IN_VOLT,
  SMB_SENSOR_XPDE_HBM_IN_CURR,
  SMB_SENSOR_XPDE_HBM_IN_POWER,
  SMB_SENSOR_XPDE_HBM_OUT_VOLT,
  SMB_SENSOR_XPDE_HBM_OUT_CURR,
  SMB_SENSOR_XPDE_HBM_OUT_POWER,
  SMB_SENSOR_XPDE_HBM_TEMP1,
  SMB_SENSOR_LM75B_U28_TEMP,
  SMB_SENSOR_LM75B_U25_TEMP,
  SMB_SENSOR_LM75B_U56_TEMP,
  SMB_SENSOR_LM75B_U55_TEMP,
  SMB_SENSOR_LM75B_U2_TEMP,
  SMB_SENSOR_LM75B_U13_TEMP,
  SMB_SENSOR_TMP421_U62_TEMP,
  SMB_SENSOR_TMP421_U63_TEMP,
  SMB_SENSOR_BMC_LM75B_TEMP,
  SMB_DOM1_MAX_TEMP,
  SMB_DOM2_MAX_TEMP,
  /* 10 internal temp sensors within GB switch */
  SMB_SENSOR_GB_HIGH_TEMP,
  SMB_SENSOR_GB_TEMP1,
  SMB_SENSOR_GB_TEMP2,
  SMB_SENSOR_GB_TEMP3,
  SMB_SENSOR_GB_TEMP4,
  SMB_SENSOR_GB_TEMP5,
  SMB_SENSOR_GB_TEMP6,
  SMB_SENSOR_GB_TEMP7,
  SMB_SENSOR_GB_TEMP8,
  SMB_SENSOR_GB_TEMP9,
  SMB_SENSOR_GB_TEMP10,
  SMB_SENSOR_GB_HBM_TEMP1,
  SMB_SENSOR_GB_HBM_TEMP2,

  /* Sensors on FCM */
  SMB_SENSOR_FCM_LM75B_U1_TEMP,
  SMB_SENSOR_FCM_LM75B_U2_TEMP,
  SMB_SENSOR_FCM_HSC_IN_VOLT,
  SMB_SENSOR_FCM_HSC_IN_POWER,
  SMB_SENSOR_FCM_HSC_OUT_VOLT,
  SMB_SENSOR_FCM_HSC_OUT_CURR,
  SMB_SENSOR_FAN1_FRONT_TACH,
  SMB_SENSOR_FAN1_REAR_TACH,
  SMB_SENSOR_FAN2_FRONT_TACH,
  SMB_SENSOR_FAN2_REAR_TACH,
  SMB_SENSOR_FAN3_FRONT_TACH,
  SMB_SENSOR_FAN3_REAR_TACH,
  SMB_SENSOR_FAN4_FRONT_TACH,
  SMB_SENSOR_FAN4_REAR_TACH,
  /* Sensors on BMC*/
  SMB_BMC_ADC0_VSEN,
  SMB_BMC_ADC1_VSEN,
  SMB_BMC_ADC2_VSEN,
  SMB_BMC_ADC3_VSEN,
  SMB_BMC_ADC4_VSEN,
  SMB_BMC_ADC5_VSEN,
  SMB_BMC_ADC6_VSEN,
  SMB_BMC_ADC7_VSEN,
  SMB_BMC_ADC8_VSEN,
  SMB_BMC_ADC9_VSEN,
  SMB_BMC_ADC10_VSEN,
  SMB_BMC_ADC11_VSEN,
  /* PXE1211C */
  SMB_SENSOR_VDDCK_0_IN_VOLT,
  SMB_SENSOR_VDDCK_0_IN_CURR,
  SMB_SENSOR_VDDCK_0_IN_POWER,
  SMB_SENSOR_VDDCK_0_OUT_VOLT,
  SMB_SENSOR_VDDCK_0_OUT_CURR,
  SMB_SENSOR_VDDCK_0_OUT_POWER,
  SMB_SENSOR_VDDCK_0_TEMP,
  SMB_SENSOR_VDDCK_1_IN_VOLT,
  SMB_SENSOR_VDDCK_1_IN_CURR,
  SMB_SENSOR_VDDCK_1_IN_POWER,
  SMB_SENSOR_VDDCK_1_OUT_VOLT,
  SMB_SENSOR_VDDCK_1_OUT_CURR,
  SMB_SENSOR_VDDCK_1_OUT_POWER,
  SMB_SENSOR_VDDCK_1_TEMP,
  SMB_SENSOR_VDDCK_2_IN_VOLT,
  SMB_SENSOR_VDDCK_2_IN_CURR,
  SMB_SENSOR_VDDCK_2_IN_POWER,
  SMB_SENSOR_VDDCK_2_OUT_VOLT,
  SMB_SENSOR_VDDCK_2_OUT_CURR,
  SMB_SENSOR_VDDCK_2_OUT_POWER,
  SMB_SENSOR_VDDCK_2_TEMP,
  /* XPDE12284 */
  SMB_SENSOR_XDPE_LEFT_1_IN_VOLT,
  SMB_SENSOR_XDPE_LEFT_1_IN_CURR,
  SMB_SENSOR_XDPE_LEFT_1_IN_POWER,
  SMB_SENSOR_XDPE_LEFT_1_OUT_VOLT,
  SMB_SENSOR_XDPE_LEFT_1_OUT_CURR,
  SMB_SENSOR_XDPE_LEFT_1_OUT_POWER,
  SMB_SENSOR_XDPE_LEFT_1_TEMP,
  SMB_SENSOR_XDPE_LEFT_2_IN_VOLT,
  SMB_SENSOR_XDPE_LEFT_2_IN_CURR,
  SMB_SENSOR_XDPE_LEFT_2_IN_POWER,
  SMB_SENSOR_XDPE_LEFT_2_OUT_VOLT,
  SMB_SENSOR_XDPE_LEFT_2_OUT_CURR,
  SMB_SENSOR_XDPE_LEFT_2_OUT_POWER,
  SMB_SENSOR_XDPE_LEFT_2_TEMP,
  SMB_SENSOR_XDPE_RIGHT_1_IN_VOLT,
  SMB_SENSOR_XDPE_RIGHT_1_IN_CURR,
  SMB_SENSOR_XDPE_RIGHT_1_IN_POWER,
  SMB_SENSOR_XDPE_RIGHT_1_OUT_VOLT,
  SMB_SENSOR_XDPE_RIGHT_1_OUT_CURR,
  SMB_SENSOR_XDPE_RIGHT_1_OUT_POWER,
  SMB_SENSOR_XDPE_RIGHT_1_TEMP,
  SMB_SENSOR_XDPE_RIGHT_2_IN_VOLT,
  SMB_SENSOR_XDPE_RIGHT_2_IN_CURR,
  SMB_SENSOR_XDPE_RIGHT_2_IN_POWER,
  SMB_SENSOR_XDPE_RIGHT_2_OUT_VOLT,
  SMB_SENSOR_XDPE_RIGHT_2_OUT_CURR,
  SMB_SENSOR_XDPE_RIGHT_2_OUT_POWER,
  SMB_SENSOR_XDPE_RIGHT_2_TEMP,

  
};

/* Sensors on PSU */
enum {
  PSU1_SENSOR_IN_VOLT = 0x01,
  PSU1_SENSOR_12V_VOLT = 0x02,
  PSU1_SENSOR_STBY_VOLT = 0x03,
  PSU1_SENSOR_IN_CURR = 0x04,
  PSU1_SENSOR_12V_CURR = 0x05,
  PSU1_SENSOR_STBY_CURR = 0x06,
  PSU1_SENSOR_IN_POWER = 0x07,
  PSU1_SENSOR_12V_POWER = 0x08,
  PSU1_SENSOR_STBY_POWER = 0x09,
  PSU1_SENSOR_FAN_TACH = 0x0a,
  PSU1_SENSOR_TEMP1 = 0x0b,
  PSU1_SENSOR_TEMP2 = 0x0c,
  PSU1_SENSOR_TEMP3 = 0x0d,
  PSU1_SENSOR_CNT = PSU1_SENSOR_TEMP3,

  PSU2_SENSOR_IN_VOLT = 0x0e,
  PSU2_SENSOR_12V_VOLT = 0x0f,
  PSU2_SENSOR_STBY_VOLT = 0x10,
  PSU2_SENSOR_IN_CURR = 0x11,
  PSU2_SENSOR_12V_CURR = 0x12,
  PSU2_SENSOR_STBY_CURR = 0x13,
  PSU2_SENSOR_IN_POWER = 0x14,
  PSU2_SENSOR_12V_POWER = 0x15,
  PSU2_SENSOR_STBY_POWER = 0x16,
  PSU2_SENSOR_FAN_TACH = 0x17,
  PSU2_SENSOR_TEMP1 = 0x18,
  PSU2_SENSOR_TEMP2 = 0x19,
  PSU2_SENSOR_TEMP3 = 0x1a,
};

int pal_get_fru_sensor_list(uint8_t fru, uint8_t **sensor_list, int *cnt);
int pal_get_fru_discrete_list(uint8_t fru, uint8_t **sensor_list, int *cnt);
int pal_sensor_read_raw(uint8_t fru, uint8_t sensor_num, void *value);
int pal_sensor_discrete_read_raw(uint8_t fru, uint8_t sensor_num, void *value);
int pal_get_sensor_name(uint8_t fru, uint8_t sensor_num, char *name);
int pal_get_sensor_units(uint8_t fru, uint8_t sensor_num, char *units);
int pal_get_sensor_threshold(uint8_t fru, uint8_t sensor_num, uint8_t thresh, void *value);
void pal_sensor_assert_handle(uint8_t fru, uint8_t snr_num, float val, uint8_t thresh);
void pal_sensor_deassert_handle(uint8_t fru, uint8_t snr_num, float val, uint8_t thresh);
int pal_sensor_threshold_flag(uint8_t fru, uint8_t snr_num, uint16_t *flag);
int pal_init_sensor_check(uint8_t fru, uint8_t snr_num, void *snr);
int wedge400_sensor_name(uint8_t fru, uint8_t sensor_num, char *name);
int pal_sensor_thresh_init(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __PAL_SENSORS_H__ */