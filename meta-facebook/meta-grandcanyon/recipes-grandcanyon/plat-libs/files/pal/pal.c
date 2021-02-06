/*
 *
 * Copyright 2020-present Facebook. All Rights Reserved.
 *
 * This file contains code to support IPMI2.0 Specification available @
 * http://www.intel.com/content/www/us/en/servers/ipmi/ipmi-specifications.html
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
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <ctype.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>
#include <openbmc/obmc-sensors.h>
#include <openbmc/libgpio.h>
#include <facebook/fbgc_gpio.h>
#include "pal.h"

#define NUM_SERVER_FRU       1
#define NUM_NIC_FRU          1
#define NUM_BMC_FRU          1
#define MAX_FAN_NAME_LEN     32 // include the string terminal
#define MAX_PWM_LABEL_LEN    32 // include the string terminal

#define MAX_TEMP_STR_SIZE    16

#define MAX_NUM_GPIO_LED_POSTCODE       8

#define MAX_NUM_GPIO_BMC_FPGA_UART_SEL  4

#define MAX_NET_DEV_NAME_SIZE   10 // include the string terminal

const char pal_fru_list[] = "all, server, bmc, uic, dpb, scc, nic, e1s_iocm";

// export to sensor-util
const char pal_fru_list_sensor_history[] = "all, server, uic, nic, e1s_iocm";

// export to power-util
const char pal_server_list[] = "server";

// export to name-util
const char *pal_server_fru_list[NUM_SERVER_FRU] = {"server"};
const char *pal_nic_fru_list[NUM_NIC_FRU] = {"nic"};
const char *pal_bmc_fru_list[NUM_BMC_FRU] = {"bmc"};

// export to fruid-util, only support iocm of FRU_E1S_IOCM
const char pal_fru_list_print[] = "all, server, bmc, uic, dpb, scc, nic, iocm, fan0, fan1, fan2, fan3";
const char pal_fru_list_rw[] = "server, bmc, uic, nic, iocm";

// fru name list for pal_get_fru_id(), the name of FRU_E1S_IOCM could be "iocm" or "e1s_iocm"
const char *fru_str_list[][2] = {
  { "all"   , "" },
  { "server", "" },
  { "bmc"   , "" },
  { "uic"   , "" },
  { "dpb"   , "" },
  { "scc"   , "" },
  { "nic"   , "" },
  { "iocm"  , "e1s_iocm" },
  { "fan0"  , "" },
  { "fan1"  , "" },
  { "fan2"  , "" },
  { "fan3"  , "" },
};

size_t server_fru_cnt = NUM_SERVER_FRU;
size_t nic_fru_cnt  = NUM_NIC_FRU;
size_t bmc_fru_cnt  = NUM_BMC_FRU;

const char pal_pwm_list[] = "0";
const char pal_tach_list[] = "0...7";

size_t pal_pwm_cnt = 1;
size_t pal_tach_cnt = 8;

// TODO: temporary mapping table, will get from fan config after fan table is ready
uint8_t fanid2pwmid_mapping[] = {0, 0, 0, 0, 0, 0, 0, 0};

enum key_event {
  KEY_BEFORE_SET,
  KEY_AFTER_INI,
};

enum net_intf_act {
  NET_INTF_DISABLE,
  NET_INTF_ENABLE,
};

struct pal_key_cfg {
  char *name;
  char *def_val;
  int (*function)(int, void*);
} key_cfg[] = {
  /* name, default value, function */
  {"system_identify", "off", NULL},
  {"server_boot_order", "0100090203ff", NULL},
  {"server_por_cfg", "on", NULL},
  {"sysfw_ver", "0", NULL},
  {"system_identify_led_interval", "default", NULL},
  {"pwr_server_last_state", "on", NULL},
  {"system_info", "0", NULL},
  /* Add more Keys here */
  {NULL, NULL, NULL} /* This is the last key of the list */
};

char * cfg_support_key_list[] = {
  "server_por_cfg",
  "system_info",
  NULL /* This is the last key of the list */
};

typedef struct {
  uint8_t gpio;
  gpio_value_t present_gpio_value;
} fru_present_gpio;

fru_present_gpio fru_present_gpio_table[] = {
  [FRU_SERVER] = {GPIO_COMP_PRSNT_N, GPIO_VALUE_LOW},
  [FRU_SCC]    = {GPIO_SCC_LOC_INS_N, GPIO_VALUE_LOW},
  [FRU_NIC]    = {GPIO_NIC_PRSNTB3_N, GPIO_VALUE_LOW},
  [FRU_FAN0]   = {GPIO_FAN_0_INS_N, GPIO_VALUE_LOW},
  [FRU_FAN1]   = {GPIO_FAN_1_INS_N, GPIO_VALUE_LOW},
  [FRU_FAN2]   = {GPIO_FAN_2_INS_N, GPIO_VALUE_LOW},
  [FRU_FAN3]   = {GPIO_FAN_3_INS_N, GPIO_VALUE_LOW}
};

uint8_t GPIO_LED_POSTCODE_TABLE[MAX_NUM_GPIO_LED_POSTCODE] = {
  GPIO_LED_POSTCODE_0,
  GPIO_LED_POSTCODE_1,
  GPIO_LED_POSTCODE_2,
  GPIO_LED_POSTCODE_3,
  GPIO_LED_POSTCODE_4,
  GPIO_LED_POSTCODE_5,
  GPIO_LED_POSTCODE_6,
  GPIO_LED_POSTCODE_7,
};

uint8_t GPIO_BMC_FPGA_UART_SEL_TABLE[MAX_NUM_GPIO_BMC_FPGA_UART_SEL] = {
  GPIO_BMC_FPGA_UART_SEL3_R,
  GPIO_BMC_FPGA_UART_SEL2_R,
  GPIO_BMC_FPGA_UART_SEL1_R,
  GPIO_BMC_FPGA_UART_SEL0_R,
};

uint8_t GPIO_BOARD_REV_ID_TABLE[MAX_NUM_OF_BOARD_REV_ID_GPIO] = {
  GPIO_BOARD_REV_ID0,
  GPIO_BOARD_REV_ID1,
  GPIO_BOARD_REV_ID2,
};


static int
pal_key_index(char *key) {
  int i = 0;

  while(key_cfg[i].name != NULL) {
    // If Key is valid, return index
    if (!strncmp(key, key_cfg[i].name, strlen(key_cfg[i].name))) {
      return i;
    }
    i++;
  }

#ifdef DEBUG
  syslog(LOG_WARNING, "%s() invalid key - %s", __func__, key);
#endif
  return -1;
}

// Check what keys can be set by cfg-util
int
pal_cfg_key_check(char *key) {
  int i = 0;

  while(cfg_support_key_list[i] != NULL) {
    // If Key is valid and can be set, return success
    if (!strncmp(key, cfg_support_key_list[i], strlen(cfg_support_key_list[i]))) {
      return 0;
    }
    i++;
  }
  // If Key could not be set, print syslog and return -1.
  syslog(LOG_WARNING, "%s(): invalid key - %s", __func__, key);

  return -1;
}

int
pal_get_key_value(char *key, char *value) {
  int index = 0;

  // Check key is defined and valid
  if ((index = pal_key_index(key)) < 0) {
    return -1;
  }
  return kv_get(key, value, NULL, KV_FPERSIST);
}

int
pal_set_key_value(char *key, char *value) {
  int index = 0, ret = 0;
  // Check key is defined and valid
  if ((index = pal_key_index(key)) < 0) {
    return -1;
  }
  if (key_cfg[index].function) {
    ret = key_cfg[index].function(KEY_BEFORE_SET, value);
    if (ret < 0) {
      return ret;
    }
  }

  return kv_set(key, value, 0, KV_FPERSIST);
}

void
pal_dump_key_value(void) {
  int i = 0;
  char value[MAX_VALUE_LEN] = {0x0};

  while(key_cfg[i].name != NULL) {
    memset(value, 0, MAX_VALUE_LEN);

    printf("%s:", key_cfg[i].name);
    if (kv_get(key_cfg[i].name, value, NULL, KV_FPERSIST) < 0) {
      printf("\n");
    } else {
      printf("%s\n",  value);
    }
    i++;
  }
}

int
pal_set_def_key_value() {
  int i = 0;
  int ret = 0, failed_count = 0;

  for (i = 0; key_cfg[i].name != NULL; i++) {
    if ((ret = kv_set(key_cfg[i].name, key_cfg[i].def_val, 0, KV_FCREATE | KV_FPERSIST)) < 0) {
      // Ignore the error messages when the kv node already existed.
      if (errno != EEXIST) {
        syslog(LOG_WARNING, "%s(): kv_set failed. %d", __func__, ret);
        failed_count ++;
      }
    }
    if (key_cfg[i].function) {
      key_cfg[i].function(KEY_AFTER_INI, key_cfg[i].name);
    }
  }

  if (failed_count != 0) {
    return -1;
  }

  return 0;
}

int
pal_get_fru_id(char *str, uint8_t *fru) {
  int fru_id = -1;
  bool found_id = false;

  for (fru_id = FRU_ALL; fru_id <= MAX_NUM_FRUS; fru_id++) {
    if ((strncmp(str, fru_str_list[fru_id][0], MAX_FRU_CMD_STR) == 0) ||
        (strncmp(str, fru_str_list[fru_id][1], MAX_FRU_CMD_STR) == 0)) {
      *fru = fru_id;
      found_id = true;
      break;
    }
  }

  return found_id ? 0 : -1;
}

int
pal_is_fru_ready(uint8_t fru, uint8_t *status) {
  switch (fru) {
    case FRU_SERVER:
    // TODO: Get server power status and BIC ready pin
    case FRU_BMC:
    case FRU_UIC:
    case FRU_DPB:
    case FRU_SCC:
    case FRU_NIC:
    case FRU_E1S_IOCM:
    case FRU_FAN0:
    case FRU_FAN1:
    case FRU_FAN2:
    case FRU_FAN3:
      *status = 1;
      break;
    default:
      *status = 0;
      syslog(LOG_WARNING, "%s() wrong fru id 0x%02x", __func__, fru);
      return PAL_ENOTSUP;
  }

  return 0;
}

int pal_check_gpio_prsnt(uint8_t gpio, int presnt_expect) {

  int ret = GPIO_VALUE_INVALID;

  ret = gpio_get_value_by_shadow(fbgc_get_gpio_name(gpio));

  if (ret == GPIO_VALUE_INVALID) {
    syslog(LOG_ERR, "%s: failed to get gpio value: gpio%d\n",__func__, gpio);
    return -1;
  }

  if (ret == presnt_expect) {
    return FRU_PRESENT;
  } else {
    return FRU_ABSENT;
  }
}

int
pal_is_fru_prsnt(uint8_t fru, uint8_t *status) {
  int ret = 0;
  int e1s_0_ret = 0, e1s_1_ret = 0;

  switch (fru) {
    case FRU_SERVER:
    case FRU_SCC:
    case FRU_NIC:
    case FRU_FAN0:
    case FRU_FAN1:
    case FRU_FAN2:
    case FRU_FAN3:
      ret = pal_check_gpio_prsnt(fru_present_gpio_table[fru].gpio, fru_present_gpio_table[fru].present_gpio_value);
      if (ret == -1) {
        *status = 0;
        return PAL_ENOTSUP;
      }
      *status = ret;
      break;
    case FRU_E1S_IOCM:
      e1s_0_ret = pal_check_gpio_prsnt(GPIO_E1S_1_PRSNT_N, GPIO_VALUE_LOW);
      e1s_1_ret = pal_check_gpio_prsnt(GPIO_E1S_2_PRSNT_N, GPIO_VALUE_LOW);
      if (e1s_0_ret == -1 || e1s_1_ret == -1) {
        *status = 0;
        return PAL_ENOTSUP;
      }
      *status = 0;
      if ( e1s_0_ret == FRU_PRESENT) {
        *status |= E1S0_IOCM_PRESENT_BIT;
      }
      if ( e1s_1_ret == FRU_PRESENT) {
        *status |= E1S1_IOCM_PRESENT_BIT;
      }
      break;
    case FRU_BMC:
    case FRU_UIC:
    case FRU_DPB:
      *status = 1;
      break;
    default:
      *status = 0;
      syslog(LOG_WARNING, "%s() wrong fru id 0x%02x", __func__, fru);
      return PAL_ENOTSUP;
  }

  return 0;
}

int
pal_get_fruid_name(uint8_t fru, char *name) {
  return fbgc_get_fruid_name(fru, name);
}

int
pal_get_fruid_path(uint8_t fru, char *path) {
  return fbgc_get_fruid_path(fru, path);
}

int
pal_get_fruid_eeprom_path(uint8_t fru, char *path) {
  return fbgc_get_fruid_eeprom_path(fru, path);
}

int
pal_fruid_write(uint8_t fru, char *path) {
  return fbgc_fruid_write(0, path);
}

int
pal_get_fru_list(char *list) {
  snprintf(list, sizeof(pal_fru_list), pal_fru_list);
  return 0;
}

int
pal_get_fru_capability(uint8_t fru, unsigned int *caps)
{
  int ret = 0;
  switch (fru) {
    case FRU_SERVER:
      *caps = FRU_CAPABILITY_FRUID_ALL | FRU_CAPABILITY_SENSOR_ALL | FRU_CAPABILITY_POWER_ALL | FRU_CAPABILITY_POWER_12V_ALL | FRU_CAPABILITY_SERVER;
      break;
    case FRU_BMC:
      *caps = FRU_CAPABILITY_FRUID_ALL | FRU_CAPABILITY_SENSOR_ALL | FRU_CAPABILITY_MANAGEMENT_CONTROLLER;
      break;
    case FRU_NIC:
      *caps = FRU_CAPABILITY_FRUID_ALL | FRU_CAPABILITY_SENSOR_ALL | FRU_CAPABILITY_NETWORK_CARD;
      break;
    case FRU_UIC:
      *caps = FRU_CAPABILITY_FRUID_ALL | FRU_CAPABILITY_SENSOR_ALL;
      break;
    case FRU_DPB:
      *caps = FRU_CAPABILITY_FRUID_READ | FRU_CAPABILITY_SENSOR_READ;
      break;
    case FRU_SCC:
      *caps = FRU_CAPABILITY_FRUID_READ | FRU_CAPABILITY_SENSOR_READ;
      break;
    case FRU_E1S_IOCM:
      *caps = FRU_CAPABILITY_FRUID_ALL | FRU_CAPABILITY_SENSOR_ALL;
      break;
    case FRU_FAN0:
    case FRU_FAN1:
    case FRU_FAN2:
    case FRU_FAN3:
      *caps = FRU_CAPABILITY_FRUID_READ | FRU_CAPABILITY_SENSOR_READ;
      break;
    default:
      ret = -1;
      break;
  }
  return ret;
}

int
pal_get_fan_name(uint8_t fan_id, char *name) {
  if (fan_id >= pal_tach_cnt) {
    syslog(LOG_WARNING, "%s: Invalid fan index: %d", __func__, fan_id);
    return -1;
  }
  snprintf(name, MAX_FAN_NAME_LEN, "Fan %d %s", (fan_id / 2), fan_id % 2 == 0 ? "Front" : "Rear");

  return 0;
}

int
pal_get_fru_name(uint8_t fru, char *name) {
  uint8_t type = 0;

  switch(fru) {
    case FRU_SERVER:
      snprintf(name, MAX_FRU_CMD_STR, "server");
      break;
    case FRU_BMC:
      snprintf(name, MAX_FRU_CMD_STR, "bmc");
      break;
    case FRU_UIC:
      snprintf(name, MAX_FRU_CMD_STR, "uic");
      break;
    case FRU_DPB:
      snprintf(name, MAX_FRU_CMD_STR, "dpb");
      break;
    case FRU_SCC:
      snprintf(name, MAX_FRU_CMD_STR, "scc");
      break;
    case FRU_NIC:
      snprintf(name, MAX_FRU_CMD_STR, "nic");
      break;
    case FRU_E1S_IOCM:
      fbgc_common_get_chassis_type(&type);
      if (type == CHASSIS_TYPE7) {
        snprintf(name, MAX_FRU_CMD_STR, "iocm");
      } else {
        snprintf(name, MAX_FRU_CMD_STR, "e1s");
      }
      break;
   case FRU_FAN0:
      snprintf(name, MAX_FRU_CMD_STR, "fan0");
      break;
   case FRU_FAN1:
      snprintf(name, MAX_FRU_CMD_STR, "fan1");
      break;
   case FRU_FAN2:
      snprintf(name, MAX_FRU_CMD_STR, "fan2");
      break;
   case FRU_FAN3:
      snprintf(name, MAX_FRU_CMD_STR, "fan3");
      break;
   default:
      if (fru > MAX_NUM_FRUS) {
        return -1;
      }
      snprintf(name, MAX_FRU_CMD_STR, "fru%d", fru);
      break;
  }

  return 0;
}

int
pal_set_fan_speed(uint8_t fan_id, uint8_t pwm) {
  char label[MAX_PWM_LABEL_LEN] = {0};
  int zone = 0;

  if (fan_id >= pal_pwm_cnt) {
    syslog(LOG_WARNING, "%s: Invalid fan index: %d", __func__, fan_id);
    return -1;
  }
  zone = fanid2pwmid_mapping[fan_id];
  snprintf(label, sizeof(label), "pwm%d", zone);

  return sensors_write_fan(label, (float)pwm);
}

int
pal_get_pwm_value(uint8_t fan_id, uint8_t *pwm) {
  char label[MAX_PWM_LABEL_LEN] = {0};
  float value = 0;
  int ret = 0;
  int zone = 0;

  if (fan_id >= pal_tach_cnt) {
    syslog(LOG_WARNING, "%s: Invalid fan index: %d", __func__, fan_id);
    return -1;
  }
  zone = fanid2pwmid_mapping[fan_id];
  snprintf(label, sizeof(label), "pwm%d", zone);
  ret = sensors_read_fan(label, &value);
  if (ret == 0) {
    *pwm = (uint8_t)value;
  }

  return ret;
}

// GUID based on RFC4122 format @ https://tools.ietf.org/html/rfc4122
static void
pal_populate_guid(char *guid, char *str) {
  unsigned int secs = 0;
  unsigned int usecs = 0;
  struct timeval tv;
  uint8_t count = 0;
  uint8_t lsb = 0, msb = 0;
  int i = 0, clock_seq = 0;

  // Populate time
  gettimeofday(&tv, NULL);

  secs = tv.tv_sec;
  usecs = tv.tv_usec;
  guid[0] = usecs & 0xFF;
  guid[1] = (usecs >> 8) & 0xFF;
  guid[2] = (usecs >> 16) & 0xFF;
  guid[3] = (usecs >> 24) & 0xFF;
  guid[4] = secs & 0xFF;
  guid[5] = (secs >> 8) & 0xFF;
  guid[6] = (secs >> 16) & 0xFF;
  guid[7] = (secs >> 24) & 0x0F;

  // Populate version
  guid[7] |= 0x10;

  // Populate clock seq with randmom number
  srand(time(NULL));
  clock_seq = rand();
  guid[8] = clock_seq & 0xFF;
  guid[9] = (clock_seq >> 8) & 0xFF;

  // Use string to populate 6 bytes unique
  // e.g. LSP62100035 => 'S' 'P' 0x62 0x10 0x00 0x35
  count = 0;
  for (i = strlen(str)-1; i >= 0; i--) {
    if (count == 6) {
      break;
    }

    // If alphabet use the character as is
    if (isalpha(str[i])) {
      guid[15-count] = str[i];
      count++;
      continue;
    }

    // If it is 0-9, use two numbers as BCD
    lsb = str[i] - '0';
    if (i > 0) {
      i--;
      if (isalpha(str[i])) {
        i++;
        msb = 0;
      } else {
        msb = str[i] - '0';
      }
    } else {
      msb = 0;
    }
    guid[15-count] = (msb << 4) | lsb;
    count++;
  }

  // zero the remaining bytes, if any
  if (count != 6) {
    memset(&guid[10], 0, 6-count);
  }

  return;
}

// GUID for System and Device
static int
pal_get_guid(uint16_t offset, char *guid) {
  char path[MAX_FILE_PATH] = {0};
  int fd = 0;
  int ret = 0;
  ssize_t bytes_rd = 0;

  // Set path for UIC
  snprintf(path, MAX_FILE_PATH, EEPROM_PATH, I2C_UIC_BUS, UIC_FRU_ADDR);

  // check for file presence
  if (access(path, F_OK)) {
    syslog(LOG_ERR, "%s() unable to access %s: %s", __func__, path, strerror(errno));
    return -1;
  }

  fd = open(path, O_RDONLY);
  if (fd < 0) {
    syslog(LOG_ERR, "%s() unable to open %s: %s", __func__, path, strerror(errno));
    return -1;
  }

  lseek(fd, offset, SEEK_SET);

  bytes_rd = read(fd, guid, GUID_SIZE);
  if (bytes_rd != GUID_SIZE) {
    syslog(LOG_ERR, "%s() read from %s failed: %s", __func__, path, strerror(errno));
    ret = -1;
  }

  close(fd);

  return ret;
}

static int
pal_set_guid(uint16_t offset, char *guid) {
  char path[MAX_FILE_PATH] = {0};
  int fd = 0;
  int ret = 0;
  ssize_t bytes_wr = 0;

  // Set path for UIC
  snprintf(path, MAX_FILE_PATH, EEPROM_PATH, I2C_UIC_BUS, UIC_FRU_ADDR);

  // check for file presence
  if (access(path, F_OK)) {
    syslog(LOG_ERR, "%s() unable to access %s: %s", __func__, path, strerror(errno));
    return -1;
  }

  fd = open(path, O_WRONLY);
  if (fd < 0) {
    syslog(LOG_ERR, "%s() unable to open %s: %s", __func__, path, strerror(errno));
    return -1;
  }

  lseek(fd, offset, SEEK_SET);

  bytes_wr = write(fd, guid, GUID_SIZE);
  if (bytes_wr != GUID_SIZE) {
    syslog(LOG_ERR, "%s() write to %s failed: %s", __func__, path, strerror(errno));
    ret = -1;
  }

  close(fd);

  return ret;
}

int
pal_get_sys_guid(uint8_t fru, char *guid) {
  int ret = 0;

  if (guid == NULL) {
    return -1;
  }

  if (fru == FRU_SERVER) {
    ret = bic_get_sys_guid(fru, (uint8_t *)guid, GUID_SIZE);
    if (ret < 0) {
      syslog(LOG_ERR, "%s() Failed to get system GUID\n", __func__);
    }
  } else {
    ret = -1;
  }

  return ret;
}

int
pal_set_sys_guid(uint8_t fru, char *str) {
  int ret = 0;
  char guid[GUID_SIZE] = {0};

  if (str == NULL) {
    return -1;
  }

  if (fru == FRU_SERVER) {
    pal_populate_guid(guid, str);

    ret = bic_set_sys_guid(fru, (uint8_t *)guid, GUID_SIZE);
    if (ret < 0) {
      syslog(LOG_ERR, "%s() Failed to set system GUID\n", __func__);
    }
  } else {
    ret = -1;
  }

  return ret;
}

int
pal_get_dev_guid(uint8_t fru, char *guid) {
  return pal_get_guid(OFFSET_DEV_GUID, guid);
}

int
pal_set_dev_guid(uint8_t fru, char *str) {
  char guid[GUID_SIZE] = {0};

  pal_populate_guid(guid, str);
  return pal_set_guid(OFFSET_DEV_GUID, guid);
}

// Update the Identification LED for the given fru with the status
int
pal_set_id_led(uint8_t fru, enum LED_HIGH_ACTIVE status) {
  int ret = 0;
  gpio_value_t val = 0;

  if (fru != FRU_UIC) {
    return -1;
  }

  val = (status == LED_ON) ? GPIO_VALUE_HIGH : GPIO_VALUE_LOW;
  ret = gpio_set_value_by_shadow(fbgc_get_gpio_name(GPIO_BMC_LED_PWR_BTN_EN_R), val);

  return ret;
}

int
pal_set_hb_led(uint8_t status) {
  int ret = 0;
  gpio_value_t val = 0;

  if (status == LED_ON) {
    val = GPIO_VALUE_HIGH;
  } else {
    val = GPIO_VALUE_LOW;
  }

  ret = gpio_set_value_by_shadow(fbgc_get_gpio_name(GPIO_BMC_LOC_HEARTBEAT_R), val);

  return ret;
}


int
pal_set_status_led(uint8_t fru, status_led_color color) {
  int ret = 0;
  gpio_value_t val_yellow = 0, val_blue = 0;

  if (fru != FRU_UIC) {
    return -1;
  }

  switch (color) {
  case STATUS_LED_OFF:
    val_yellow = GPIO_VALUE_LOW;
    val_blue   = GPIO_VALUE_LOW;
    break;
  case STATUS_LED_BLUE:
    val_yellow = GPIO_VALUE_LOW;
    val_blue   = GPIO_VALUE_HIGH;
    break;
  case STATUS_LED_YELLOW:
    val_yellow = GPIO_VALUE_HIGH;
    val_blue   = GPIO_VALUE_LOW;
    break;
  default:
    syslog(LOG_ERR, "%s() Invalid LED color: %d\n", __func__, color);
    return -1;
  }

  if (0 != (ret = gpio_set_value_by_shadow(fbgc_get_gpio_name(GPIO_BMC_LED_STATUS_YELLOW_EN_R), val_yellow))) {
    syslog(LOG_ERR, "%s() Failed to set GPIO BMC_LED_STATUS_YELLOW_EN_R to %d\n", __func__, val_yellow);
  }
  if (0 != (ret = gpio_set_value_by_shadow(fbgc_get_gpio_name(GPIO_BMC_LED_STATUS_BLUE_EN_R), val_blue))) {
    syslog(LOG_ERR, "%s() Failed to set GPIO BMC_LED_STATUS_BLUE_EN_R to %d\n", __func__, val_blue);
  }

  return ret;
}

int
pal_set_e1s_led(uint8_t fru, e1s_led_id id, enum LED_HIGH_ACTIVE status) {
  int ret = 0;
  gpio_value_t val = 0;

  if (fru != FRU_E1S_IOCM) {
    return -1;
  }

  if (status == LED_ON) {
    val = GPIO_VALUE_HIGH;
  } else {
    val = GPIO_VALUE_LOW;
  }

  if (id == ID_E1S0_LED) {
    ret = gpio_set_value_by_shadow(fbgc_get_gpio_name(GPIO_E1S_1_LED_ACT), val);
  } else if (id == ID_E1S1_LED) {
    ret = gpio_set_value_by_shadow(fbgc_get_gpio_name(GPIO_E1S_2_LED_ACT), val);
  } else {
    return -1;
  }

  return ret;
}

int
pal_get_boot_order(uint8_t slot, uint8_t *req_data, uint8_t *boot, uint8_t *res_len) {
  int i = 0;
  int j = 0;
  int ret = 0;
  int msb = 0, lsb = 0;
  int tmp_len = 0;
  char key[MAX_KEY_LEN] = {0};
  char str[MAX_VALUE_LEN] = {0};
  char tmp_str[4] = {0};

  tmp_len = sizeof(tmp_str);

  snprintf(key, MAX_KEY_LEN, "server_boot_order");

  ret = pal_get_key_value(key, str);
  if (ret != 0) {
    *res_len = 0;
    return ret;
  }

  for (i = 0; i < 2*SIZE_BOOT_ORDER; i += 2) {
    snprintf(tmp_str, tmp_len, "%c\n", str[i]);
    msb = strtol(tmp_str, NULL, 16);

    snprintf(tmp_str, tmp_len, "%c\n", str[i+1]);
    lsb = strtol(tmp_str, NULL, 16);
    boot[j++] = (msb << 4) | lsb;
  }

  *res_len = SIZE_BOOT_ORDER;

  return 0;
}

int
pal_set_boot_order(uint8_t slot, uint8_t *boot, uint8_t *res_data, uint8_t *res_len) {
  int i = 0;
  int tmp_len = 0;
  char key[MAX_KEY_LEN] = {0};
  char str[MAX_VALUE_LEN] = {0};
  char tmp_str[4] = {0};

  *res_len = 0;
  tmp_len = sizeof(tmp_str);

  snprintf(key, MAX_KEY_LEN, "server_boot_order");

  for (i = 0; i < SIZE_BOOT_ORDER; i++) {
    snprintf(tmp_str, tmp_len, "%02x", boot[i]);
    strncat(str, tmp_str, tmp_len);
  }

  return pal_set_key_value(key, str);
}

bool
pal_is_fw_update_ongoing_system(void) {
  uint8_t i = 0;

  for (i = FRU_SERVER; i < FRU_CNT; i++) {
    if (pal_is_fw_update_ongoing(i) == true) {
      return true;
    }
  }

  return false;
}

int
pal_get_nic_fru_id(void) {
  return FRU_NIC;
}

int
pal_is_slot_server(uint8_t fru) {
  return (fru == FRU_SERVER) ? 1 : 0;
}

int
pal_set_sysfw_ver(uint8_t slot, uint8_t *ver) {
  int i = 0, ret = 0;
  int tmp_len = 0;
  char key[MAX_KEY_LEN] = {0};
  char str[MAX_VALUE_LEN] = {0};
  char tmp_str[MAX_TEMP_STR_SIZE] = {0};

  if (ver == NULL) {
    syslog(LOG_ERR, "%s() Pointer \"ver\" is NULL.\n", __func__);
    return -1;
  }

  snprintf(key, sizeof(key), "sysfw_ver");

  for (i = 0; i < SIZE_SYSFW_VER; i++) {
    tmp_len = sizeof(tmp_str);

    memset(tmp_str, 0, sizeof(tmp_str));
    snprintf(tmp_str, sizeof(tmp_str), "%02x", ver[i]);
    strncat(str, tmp_str, tmp_len);
  }

  ret = pal_set_key_value(key, str);
  if (ret < 0) {
    syslog(LOG_WARNING, "%s: failed to set key value %s.", __func__, key);
  }

  return ret;
}

int
pal_get_sysfw_ver(uint8_t slot, uint8_t *ver) {
  int i = 0, j = 0;
  int ret = 0;
  char key[MAX_KEY_LEN] = {0};
  char str[MAX_VALUE_LEN] = {0};
  char tmp_str[MAX_TEMP_STR_SIZE] = {0};

  if (ver == NULL) {
    syslog(LOG_ERR, "%s() Pointer \"ver\" is NULL.\n", __func__);
    return -1;
  }

  snprintf(key, sizeof(key), "sysfw_ver");
  ret = pal_get_key_value(key, str);
  if (ret != 0) {
    syslog(LOG_WARNING, "%s() Failed to run pal_get_key_value. key:%s", __func__, key);
    return PAL_ENOTSUP;
  }

  for (i = 0; i < 2*SIZE_SYSFW_VER; i += 2) {
    snprintf(tmp_str, sizeof(tmp_str), "%c%c\n", str[i], str[i+1]);
    ver[j++] = (uint8_t) strtol(tmp_str, NULL, 16);
  }

  return ret;
}

// Use part of the function for IPMI OEM Command "CMD_OEM_GET_POSS_PCIE_CONFIG" 0xF4
int pal_get_poss_pcie_config(uint8_t slot, uint8_t *req_data, uint8_t req_len, uint8_t *res_data, uint8_t *res_len) {
  int ret = 0;
  uint8_t chassis_type = 0;
  get_pcie_config_response response;

  memset(&response, 0, sizeof(response));
  response.completion_code = CC_UNSPECIFIED_ERROR;

  if (req_data == NULL) {
    syslog(LOG_WARNING, "%s(): fail to get PCIe configuration beacuse parameter: *req_data is NULL pointer", __func__);
    return response.completion_code;
  }

  if (res_data == NULL) {
    syslog(LOG_WARNING, "%s(): fail to get PCIe configuration beacuse parameter: *res_data is NULL pointer", __func__);
    return response.completion_code;
  }

  if (res_len == NULL) {
    syslog(LOG_WARNING, "%s(): fail to get PCIe configuration beacuse parameter: *res_len is NULL pointer", __func__);
    return response.completion_code;
  }

  ret = fbgc_common_get_chassis_type(&chassis_type);
  if ((ret == 0) && (chassis_type == CHASSIS_TYPE5)) {
    response.pcie_cfg = PCIE_CONFIG_TYPE5;
  } else if ((ret == 0) && (chassis_type == CHASSIS_TYPE7)) {
    response.pcie_cfg = PCIE_CONFIG_TYPE7;
  } else {
    syslog(LOG_WARNING, "%s(): fail to get PCIe configuration because fbgc_common_get_chassis_type() error", __func__);
    return response.completion_code;
  }

  memcpy(res_data, &response.pcie_cfg, MIN(MAX_IPMI_MSG_SIZE, sizeof(response.pcie_cfg)));
  *res_len = sizeof(response.pcie_cfg);
  response.completion_code = CC_SUCCESS;

  return response.completion_code;
}

int
pal_add_i2c_device(uint8_t bus, uint8_t addr, char *device_name) {
  int ret = -1;
  char cmd[MAX_PATH_LEN] = {0};

  if (device_name == NULL) {
    syslog(LOG_ERR, "%s device name is null", __func__);
    return -1;
  }

  snprintf(cmd, sizeof(cmd),
            "echo %s %d > /sys/class/i2c-dev/i2c-%d/device/new_device",
              device_name, addr, bus);

#if DEBUG
  syslog(LOG_WARNING, "%s Cmd: %s", __func__, cmd);
#endif

  ret = run_command(cmd);

  return ret;
}

int
pal_del_i2c_device(uint8_t bus, uint8_t addr) {
  int ret = -1;
  char cmd[MAX_PATH_LEN] = {0};

  snprintf(cmd, sizeof(cmd), "echo %d > /sys/class/i2c-dev/i2c-%d/device/delete_device",
           addr, bus);

#if DEBUG
  syslog(LOG_WARNING, "%s Cmd: %s", __func__, cmd);
#endif

  ret = run_command(cmd);

  return ret;
}

int
pal_bind_i2c_device(uint8_t bus, uint8_t addr, char *driver_name) {
  int ret = -1;
  char cmd[MAX_PATH_LEN] = {0};

  if (driver_name == NULL) {
    syslog(LOG_ERR, "%s driver name is null", __func__);
    return -1;
  }

  snprintf(cmd, sizeof(cmd),
            "echo %d-00%d > /sys/bus/i2c/drivers/%s/bind",
              bus, addr, driver_name);

#if DEBUG
  syslog(LOG_WARNING, "%s Cmd: %s", __func__, cmd);
#endif

  ret = run_command(cmd);

  return ret;
}

int
pal_unbind_i2c_device(uint8_t bus, uint8_t addr, char *driver_name) {
  int ret = -1;
  char cmd[MAX_PATH_LEN] = {0};

  if (driver_name == NULL) {
    syslog(LOG_ERR, "%s driver name is null", __func__);
    return -1;
  }

  snprintf(cmd, sizeof(cmd),
            "echo %d-00%d > /sys/bus/i2c/drivers/%s/unbind",
              bus, addr, driver_name);

#if DEBUG
  syslog(LOG_WARNING, "%s Cmd: %s", __func__, cmd);
#endif

  ret = run_command(cmd);

  return ret;
}

// To get the platform sku
int pal_get_sku(platformInformation *pal_sku) {
  int i = 0;
  int ret = 0;
  int pal_sku_size = 0, pal_sku_value = 0;
  uint8_t tmp_pal_sku[SKU_SIZE] = {0};
  char key[MAX_KEY_LEN] = {0};
  char str[MAX_VALUE_LEN] = {0};

  if (pal_sku == NULL) {
    syslog(LOG_ERR, "%s(): Failed to get platform SKU because parameter is NULL\n", __func__);
    return -1;
  }

  pal_sku_size = sizeof(platformInformation);

  // PAL_SKU[0:1] = {UIC_ID0, UIC_ID1}
  // PAL_SKU[2:5] = {UIC_TYPE0, UIC_TYPE1, UIC_TYPE2, UIC_TYPE3}
  snprintf(key, MAX_KEY_LEN, "system_info");
  ret = pal_get_key_value(key, str);

  if (ret < 0) {
    syslog(LOG_ERR, "%s(): Failed to get platform SKU because failed to get key value of %s\n", __func__, key);
    return -1;
  }

  pal_sku_value = atoi(str);

  if (pal_sku_value >= MAX_SKU_VALUE) {
    syslog(LOG_WARNING, "%s(): Failed to get platform SKU because SKU value is wrong\n", __func__);
    return -1;
  } else {
    for (i = pal_sku_size - 1; i >= 0; i--) {
      tmp_pal_sku[i] = (pal_sku_value & 1) + '0';
      pal_sku_value = pal_sku_value >> 1;
    }

    memcpy(pal_sku, tmp_pal_sku, pal_sku_size);
  }

  return ret;
}

// To get the UIC location
int pal_get_uic_location(uint8_t *uic_id){
  int ret = 0;

  // Add one byte of NULL for converting string to integer.
  char tmp_uic_id[SKU_UIC_ID_SIZE + 1] = {0};
  platformInformation pal_sku;
  memset(&pal_sku, 0, sizeof(pal_sku));

  if (uic_id == NULL) {
    syslog(LOG_ERR, "%s(): Failed to get UIC location because parameter is NULL\n", __func__);
    return -1;
  }

  // UIC_ID[0:1]: 01=UIC_A; 10=UIC_B
  ret = pal_get_sku(&pal_sku);
  if (ret < 0) {
    syslog(LOG_WARNING, "%s(): Failed to get UIC location because failed to get sku value\n", __func__);
    return -1;
  }

  memcpy(tmp_uic_id, pal_sku.uicId, MIN(sizeof(tmp_uic_id), sizeof(pal_sku.uicId)));
  *uic_id = (uint8_t) strtol(tmp_uic_id, NULL, 2);

  return ret;
}

//For IPMI OEM command "CMD_OEM_GET_PLAT_INFO" 0x7E
int pal_get_plat_sku_id(void){
  uint8_t uic_type = 0;
  uint8_t uic_location = 0;
  int platform_info = 0;

  if ((fbgc_common_get_chassis_type(&uic_type) < 0) || (pal_get_uic_location(&uic_location) < 0)) {
    return -1;
  }

  if(uic_type == CHASSIS_TYPE5) {
    if(uic_location == UIC_SIDEA) {
      platform_info = PLAT_INFO_SKUID_TYPE5A;
    } else if(uic_location == UIC_SIDEB) {
      platform_info = PLAT_INFO_SKUID_TYPE5B;
    } else {
      return -1;
    }
  } else if (uic_type == CHASSIS_TYPE7) {
    platform_info = PLAT_INFO_SKUID_TYPE7_HEADNODE;
  } else {
    return -1;
  }

  return platform_info;
}

int
pal_copy_eeprom_to_bin(const char *eeprom_file, const char *bin_file) {
  int eeprom = 0;
  int bin = 0;
  int ret = 0;
  uint8_t tmp[FRUID_SIZE] = {0};
  ssize_t bytes_rd = 0, bytes_wr = 0;

  errno = 0;

  if (eeprom_file == NULL || bin_file == NULL) {
    syslog(LOG_ERR, "%s: invalid parameter", __func__);
    return -1;
  }

  eeprom = open(eeprom_file, O_RDONLY);
  if (eeprom < 0) {
    syslog(LOG_ERR, "%s: unable to open the %s file: %s",
	__func__, eeprom_file, strerror(errno));
    return -1;
  }

  bin = open(bin_file, O_WRONLY | O_CREAT, 0644);
  if (bin < 0) {
    syslog(LOG_ERR, "%s: unable to create %s file: %s",
	__func__, bin_file, strerror(errno));
    ret = -1;
    goto err;
  }

  bytes_rd = read(eeprom, tmp, FRUID_SIZE);
  if (bytes_rd < 0) {
    syslog(LOG_ERR, "%s: read %s file failed: %s",
	__func__, eeprom_file, strerror(errno));
    ret = -1;
    goto exit;
  } else if (bytes_rd < FRUID_SIZE) {
    syslog(LOG_ERR, "%s: less than %d bytes", __func__, FRUID_SIZE);
    ret = -1;
    goto exit;
  }

  bytes_wr = write(bin, tmp, bytes_rd);
  if (bytes_wr != bytes_rd) {
    syslog(LOG_ERR, "%s: write to %s file failed: %s",
	__func__, bin_file, strerror(errno));
    ret = -1;
  }

exit:
  close(bin);
err:
  close(eeprom);

  return ret;
}

int
pal_post_display(uint8_t status) {
  int ret = 0, i = 0;
  gpio_value_t value = GPIO_VALUE_INVALID;

  for (i = 0; i < MAX_NUM_GPIO_LED_POSTCODE; i++) {
    if (BIT(status, i) != 0) {
      value = GPIO_VALUE_HIGH;
    } else {
      value = GPIO_VALUE_LOW;
    }
    ret = gpio_set_value_by_shadow(fbgc_get_gpio_name(GPIO_LED_POSTCODE_TABLE[i]), value);
    if (ret < 0) {
      syslog(LOG_WARNING, "%s fail to display post code to debug card, failed gpio: LED_POSTCODE_%d, ret: %d\n", __func__, i, ret);
      break;
    }
  }

  return ret;
}

int
pal_get_current_led_post_code(uint8_t *post_code) {
  int i = 0;
  gpio_value_t value = GPIO_VALUE_INVALID;

  if (post_code == NULL) {
    syslog(LOG_ERR, "%s Invalid parameter: post_code is NULL\n", __func__);
    return -1;
  }

  *post_code = 0;

  for (i = (MAX_NUM_GPIO_LED_POSTCODE - 1); i >= 0; i--) {
    value = gpio_get_value_by_shadow(fbgc_get_gpio_name(GPIO_LED_POSTCODE_TABLE[i]));
    if (value == GPIO_VALUE_INVALID) {
      syslog(LOG_WARNING, "%s fail to get post code, failed gpio: LED_POSTCODE_%d\n", __func__, i);
      return -1;
    }
    // convert GPIOs to number
    (*post_code) <<= 1;
    (*post_code) |= (uint8_t)value;
  }

  return 0;
}

int
pal_get_debug_card_uart_sel(uint8_t *uart_sel) {
  int i = 0;
  gpio_value_t val = GPIO_VALUE_INVALID;

  if (uart_sel == NULL) {
    syslog(LOG_ERR, "%s Invalid parameter: UART selection\n", __func__);
    return -1;
  }

  *uart_sel = 0;

  for (i = 0; i < MAX_NUM_GPIO_BMC_FPGA_UART_SEL; i++) {
    val = gpio_get_value_by_shadow(fbgc_get_gpio_name(GPIO_BMC_FPGA_UART_SEL_TABLE[i]));
    if (val == GPIO_VALUE_INVALID) {
      syslog(LOG_WARNING, "%s() Can not get GPIO_BMC_FPGA_UART_SEL%d", __func__, i);
      return -1;
    }
    // convert GPIOs to number
    (*uart_sel) <<= 1;
    (*uart_sel) |= (uint8_t)val;
  }

  return 0;
}

int
pal_is_debug_card_present(uint8_t *status) {
  int present_status = 0;
  int ret = 0;

  if (status == NULL) {
    syslog(LOG_ERR, "%s Invalid parameter: present status\n", __func__);
    return -1;
  }

  present_status = pal_check_gpio_prsnt(GPIO_DEBUG_CARD_PRSNT_N, GPIO_VALUE_LOW);

  switch (present_status) {
    case FRU_PRESENT:
    case FRU_ABSENT:
      *status = present_status;
      break;
    default:
      syslog(LOG_ERR, "%s failed to get debug card present gpio, status: %d\n", __func__, present_status);
      ret = -1;
      break;
  }

  return ret;
}

int
pal_post_handle(uint8_t slot, uint8_t postcode) {
  uint8_t present_status = 0, uart_sel = 0;
  int ret = 0;

  ret = pal_is_debug_card_present(&present_status);
  if (ret < 0) {
    syslog(LOG_ERR, "%s: failed to get debug card present status. ret code: %d\n", __func__, ret);
    return ret;
  }

  if (present_status == FRU_ABSENT) {
    return 0;
  }

  ret = pal_get_debug_card_uart_sel(&uart_sel);
  if (ret < 0) {
    return ret;
  }

  if (uart_sel == DEBUG_UART_SEL_BMC) { // Do not overwrite BMC error code
    return 0;
  }

  ret = pal_post_display(postcode);

  return ret;
}

int
pal_get_fan_latch(uint8_t *chassis_status) {
  gpio_value_t fan_latch_status = GPIO_VALUE_INVALID;

  if (chassis_status == NULL) {
    syslog(LOG_WARNING, "%s() failed to get the status of fan latch due to the NULL parameter.", __func__);
    return -1;
  }

  fan_latch_status = gpio_get_value_by_shadow(fbgc_get_gpio_name(GPIO_DRAWER_CLOSED_N));

  if (fan_latch_status == GPIO_VALUE_INVALID) {
    syslog(LOG_WARNING, "%s() failed to get the status of fan latch due to the invalid gpio value.", __func__);
    return -1;
  }

  if (fan_latch_status == GPIO_VALUE_HIGH) {
    *chassis_status = CHASSIS_OUT;
  } else {
    *chassis_status = CHASSIS_IN;
  }

  return 0;
}

void
pal_specific_plat_fan_check(bool status)
{
  uint8_t chassis_status = 0;

  if (pal_get_fan_latch(&chassis_status) < 0) {
    syslog(LOG_WARNING, "%s: Get chassis status in/out failed.", __func__);
    return;
  }

  if(chassis_status == CHASSIS_OUT) {
    printf("Chassis Fan Latch Open: True\n");
  } else {
    printf("Chassis Fan Latch Open: False\n");
  }

  return;
}

// IPMI OEM Command 
// netfn: NETFN_OEM_1S_REQ (0x30)
// command code: CMD_OEM_BYPASS_CMD (0x34)
int
pal_bypass_cmd(uint8_t slot, uint8_t *req_data, uint8_t req_len, uint8_t *res_data, uint8_t *res_len) {
  int ret = 0;
  int completion_code = CC_SUCCESS;
  uint8_t netfn = 0, cmd = 0;
  uint8_t tlen = 0, rlen = 0;
  uint8_t prsnt_status = 0, pwr_status = 0;  
  uint8_t netdev = 0;
  uint8_t action = 0;
  uint8_t tbuf[MAX_IPMB_REQ_LEN] = {0};
  uint8_t rbuf[MAX_IPMB_RES_LEN] = {0};
  char sendcmd[MAX_SYS_CMD_REQ_LEN] = {0};
  ipmi_req_t* ipmi_req = (ipmi_req_t*)tbuf;
  ipmi_res_t* ipmi_resp = (ipmi_res_t*)rbuf;
  NCSI_NL_MSG_T *msg = NULL;
  NCSI_NL_RSP_T *rsp = NULL;
  bypass_ncsi_req ncsi_req = {0};
  network_cmd net_req = {0};

  if (req_data == NULL) {
    syslog(LOG_WARNING, "%s(): NULL request data, can not bypass the command", __func__);
    return CC_INVALID_PARAM;
  }
  if (res_data == NULL) {
    syslog(LOG_WARNING, "%s(): NULL response data, can not bypass the command", __func__);
    return CC_INVALID_PARAM;
  }
  if (res_len == NULL) {
    syslog(LOG_WARNING, "%s(): NULL response length, can not bypass the command", __func__);
    return CC_INVALID_PARAM;
  }
  *res_len = 0;

  ret = pal_is_fru_prsnt(FRU_SERVER, &prsnt_status);
  if (ret < 0) {
    syslog(LOG_WARNING, "%s(): Can not bypass the command due to get server present status failed", __func__);
    return CC_UNSPECIFIED_ERROR;
  }
  if (prsnt_status == FRU_ABSENT) {
    syslog(LOG_WARNING, "%s(): Can not bypass the command due to server absent", __func__);
    return CC_NOT_SUPP_IN_CURR_STATE;
  }

  ret = pal_get_server_12v_power(&pwr_status);
  if (ret < 0) {
    syslog(LOG_WARNING, "%s(): Can not bypass the command due to get server 12V power status failed", __func__);
    return CC_UNSPECIFIED_ERROR;
  }
  if (pwr_status == SERVER_12V_OFF) {
    syslog(LOG_WARNING, "%s(): Can not bypass the command due to server 12V power off", __func__);
    return CC_NOT_SUPP_IN_CURR_STATE;
  }
  memset(tbuf, 0, sizeof(tbuf));
  memset(rbuf, 0, sizeof(rbuf));

  switch (((bypass_cmd*)req_data)->target) {
    case BYPASS_BIC:
      if ((req_len < sizeof(bypass_ipmi_header)) || (req_len > sizeof(tbuf))) {
        completion_code = CC_INVALID_LENGTH;
        break;
      }
      tlen = req_len - sizeof(bypass_ipmi_header);
      memcpy(ipmi_req, &((bypass_cmd*)req_data)->data[0], sizeof(tbuf));
      netfn = ipmi_req->netfn_lun;
      cmd = ipmi_req->cmd;

      // Bypass command to Bridge IC
      if (tlen != 0) {
        ret = bic_ipmb_wrapper(netfn, cmd, (uint8_t*)ipmi_req->data, tlen, res_data, res_len);
      } else {
        ret = bic_ipmb_wrapper(netfn, cmd, NULL, 0, res_data, res_len);
      }
      if (ret < 0) {
        syslog(LOG_WARNING, "%s(): Failed to bypass IPMI command to BIC", __func__);
        completion_code = CC_UNSPECIFIED_ERROR;
      }
      break;

    case BYPASS_ME:
      if ((req_len < sizeof(bypass_ipmi_header)) || (req_len > sizeof(tbuf))) {
        completion_code = CC_INVALID_LENGTH;
        break;
      }
      tlen = req_len - sizeof(bypass_me_header);

      memcpy(ipmi_req, &((bypass_cmd*)req_data)->data[0], sizeof(tbuf));
      ipmi_req->netfn_lun = IPMI_NETFN_SHIFT(ipmi_req->netfn_lun);

      // Bypass command to ME
      ret = bic_me_xmit((uint8_t*)ipmi_req, tlen, (uint8_t*)(&ipmi_resp->cc), &rlen);
      if (ret == 0) {
        completion_code = ipmi_resp->cc;
        memcpy(&res_data[0], ipmi_resp->data, (rlen - sizeof(bypass_me_resp_header)));
        *res_len = rlen - sizeof(bypass_me_resp_header);
      } else {
        syslog(LOG_WARNING, "%s(): Failed to send IPMI command to ME", __func__);
        completion_code = CC_UNSPECIFIED_ERROR;
      }
      break;

    case BYPASS_NCSI:
      if ((req_len < sizeof(bypass_ncsi_header)) || (req_len > sizeof(NCSI_NL_MSG_T))) {
        completion_code = CC_INVALID_LENGTH;
        break;
      }
      tlen = req_len - sizeof(bypass_ncsi_header); 
      msg = calloc(1, sizeof(NCSI_NL_MSG_T));
      if (msg == NULL) {
        syslog(LOG_ERR, "%s(): failed msg buffer allocation", __func__);
        completion_code = CC_UNSPECIFIED_ERROR;
        break;
      }
      memset(&ncsi_req, 0, sizeof(ncsi_req));
      memcpy(&ncsi_req, &((bypass_cmd*)req_data)->data[0], sizeof(ncsi_req));
      memset(msg, 0, sizeof(*msg));
      snprintf(msg->dev_name, MAX_NET_DEV_NAME_SIZE, "eth%d", ncsi_req.netdev);

      msg->channel_id = ncsi_req.channel;
      msg->cmd = ncsi_req.cmd;
      msg->payload_length = tlen;
      memcpy(&msg->msg_payload[0], &ncsi_req.data[0], NCSI_MAX_PAYLOAD);

      rsp = send_nl_msg_libnl(msg);
      if (rsp != NULL) {
        memcpy(&res_data[0], &rsp->msg_payload[0], rsp->hdr.payload_length);
        *res_len = rsp->hdr.payload_length;
      } else {
        completion_code = CC_UNSPECIFIED_ERROR;
      }
      break;

    case BYPASS_NETWORK:
      if (req_len != sizeof(bypass_network_header)) {
        completion_code = CC_INVALID_LENGTH;
        break;
      }
      memset(&net_req, 0, sizeof(net_req));
      memcpy(&net_req, &((bypass_cmd*)req_data)->data[0], sizeof(net_req));
      netdev = net_req.netdev;
      action = net_req.action;
      if (action == NET_INTF_ENABLE) {        
        snprintf(sendcmd, sizeof(sendcmd), "ifup eth%d", netdev);
      } else if (action == NET_INTF_DISABLE) {
        snprintf(sendcmd, sizeof(sendcmd), "ifdown eth%d", netdev);
      } else {
        completion_code = CC_INVALID_PARAM;
        break;
      }
      ret = run_command(sendcmd);
      if (ret != 0) {
        syslog(LOG_WARNING, "%s(): sytem command: %s failed, error: %s", __func__, sendcmd, strerror(errno));
        completion_code = CC_UNSPECIFIED_ERROR;
      }
      break;

    default:
      completion_code = CC_NOT_SUPP_IN_CURR_STATE;
      break;
  }

  if (msg != NULL) {
    free(msg);
  }
  if (rsp != NULL) {
    free(rsp);
  }

  return completion_code;
}

int
pal_get_uic_board_id(uint8_t *board_id) {
  gpio_value_t val = GPIO_VALUE_INVALID;
  int i = 0;

  if (board_id == NULL) {
    syslog(LOG_ERR, "%s Invalid parameter: board id\n", __func__);
    return -1;
  }

  *board_id = 0;

  for (i = 0; i < MAX_NUM_OF_BOARD_REV_ID_GPIO; i++) {
    val = gpio_get_value_by_shadow(fbgc_get_gpio_name(GPIO_BOARD_REV_ID_TABLE[i]));
    if (val == GPIO_VALUE_INVALID) {
      syslog(LOG_WARNING, "%s() Can not get GPIO_BOARD_REV_ID%d", __func__, i);
      return -1;
    }
    // convert GPIOs to number
    (*board_id) <<= 1;
    (*board_id) |= (uint8_t)val;
  }

  return 0;
}
