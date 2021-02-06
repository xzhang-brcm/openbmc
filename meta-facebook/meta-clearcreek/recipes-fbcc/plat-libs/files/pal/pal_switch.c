/*
 *
 * Copyright 2019-present Facebook. All Rights Reserved.
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
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>
#include <openbmc/libgpio.h>
#include <switchtec/switchtec.h>
#include <switchtec/gas.h>
#include "pal.h"

#define PAX_I2C_LOCK "/tmp/pax_lock"

static int pax_lock()
{
  int fd;

  fd = open(PAX_I2C_LOCK, O_RDONLY | O_CREAT, 0666);
  if (fd < 0)
    return -1;

  if (flock(fd, LOCK_EX) < 0) {
    syslog(LOG_WARNING, "%s: errno = %d", __func__, errno);
    return -1;
  }

  return fd;
}

static int pax_unlock(int fd)
{
  if (fd < 0)
    return -1;

  if (flock(fd, LOCK_UN) < 0) {
    syslog(LOG_WARNING, "%s: errno = %d", __func__, errno);
  }

  close(fd);
  return 0;
}

int pal_check_pax_fw_type(uint8_t comp, const char *fwimg)
{
  int fd;
  struct switchtec_fw_image_info info;

  fd = open(fwimg, O_RDONLY);
  if (fd < 0 || switchtec_fw_file_info(fd, &info) < 0)
    return -1;

  close(fd);

  if ((comp == PAX_BL2 && info.type == SWITCHTEC_FW_TYPE_BL2) ||
      (comp == PAX_IMG && info.type == SWITCHTEC_FW_TYPE_IMG) ||
      (comp == PAX_CFG && info.type == SWITCHTEC_FW_TYPE_CFG) ) {
    return 0;
  } else {
    return -1;
  }
}

void pal_clear_pax_cache(uint8_t paxid)
{
  char key[MAX_KEY_LEN];
  snprintf(key, sizeof(key), "pax%d_bl2", paxid);
  kv_del(key, 0);
  snprintf(key, sizeof(key), "pax%d_img", paxid);
  kv_del(key, 0);
  snprintf(key, sizeof(key), "pax%d_cfg", paxid);
  kv_del(key, 0);
}

int pal_pax_fw_update(uint8_t paxid, const char *fwimg)
{
  int fd, ret;
  char cmd[128] = {0};

  if (pal_is_server_off())
    return -1;

  snprintf(cmd, sizeof(cmd), SWITCHTEC_UPDATE_CMD, SWITCH_BASE_ADDR + paxid, fwimg);
  fd = pax_lock();
  if (fd < 0)
    return -1;

  ret = system(cmd);
  pax_unlock(fd);
  if (WIFEXITED(ret) && (WEXITSTATUS(ret) == 0)) {
    return 0;
  } else {
    return -1;
  }
}

int pal_read_pax_dietemp(uint8_t sensor_num, float *value)
{
  int fd, ret = 0;
  uint8_t addr;
  uint8_t paxid = sensor_num - 0;
  uint32_t temp, sub_cmd_id;
  char device_name[LARGEST_DEVICE_NAME] = {0};
  struct switchtec_dev *dev;

  if (pal_is_server_off())
    return ERR_SENSOR_NA;

  addr = SWITCH_BASE_ADDR + paxid;
  snprintf(device_name, LARGEST_DEVICE_NAME, SWITCHTEC_DEV, addr);

  fd = pax_lock();
  if (fd < 0)
    return ERR_SENSOR_NA;

  dev = switchtec_open(device_name);
  if (dev == NULL) {
    syslog(LOG_WARNING, "%s: switchtec open %s failed", __func__, device_name);
    pax_unlock(fd);
    return ERR_SENSOR_NA;
  }

  sub_cmd_id = MRPC_DIETEMP_GET_GEN4;
  ret = switchtec_cmd(dev, MRPC_DIETEMP, &sub_cmd_id,
                      sizeof(sub_cmd_id), &temp, sizeof(temp));
  switchtec_close(dev);
  pax_unlock(fd);

  if (ret == 0)
    *value = (float) temp / 100.0;

  return ret < 0? ERR_SENSOR_NA: 0;
}

static struct switchtec_fw_part_summary* get_pax_ver_sum(uint8_t paxid)
{
  int fd;
  char device_name[LARGEST_DEVICE_NAME] = {0};
  struct switchtec_dev *dev;
  struct switchtec_fw_part_summary *sum;

  snprintf(device_name, LARGEST_DEVICE_NAME, SWITCHTEC_DEV, SWITCH_BASE_ADDR + paxid);

  fd = pax_lock();
  if (fd < 0)
    return NULL;

  dev = switchtec_open(device_name);
  if (dev == NULL) {
    syslog(LOG_WARNING, "%s: switchtec open %s failed", __func__, device_name);
    return NULL;
  }

  sum = switchtec_fw_part_summary(dev);
  if (sum == NULL)
    syslog(LOG_WARNING, "%s: switchtec get %s version failed", __func__, device_name);

  switchtec_close(dev);
  pax_unlock(fd);

  return sum;
}
int pal_get_pax_bl2_ver(uint8_t paxid, char *ver)
{
  struct switchtec_fw_part_summary *sum;
  char img_key[MAX_KEY_LEN];
  char bl2_key[MAX_KEY_LEN];

  snprintf(img_key, sizeof(img_key), "pax%d_img", paxid);
  snprintf(bl2_key, sizeof(bl2_key), "pax%d_bl2", paxid);
  if (kv_get(bl2_key, ver, NULL, 0) == 0)
    return 0;

  if (pal_is_server_off())
    return -1;

  sum = get_pax_ver_sum(paxid);
  if (sum == NULL)
    return -1;

  snprintf(ver, MAX_VALUE_LEN, "%s", sum->img.active->version);
  kv_set(img_key, ver, 0, 0);
  snprintf(ver, MAX_VALUE_LEN, "%s", sum->bl2.active->version);
  kv_set(bl2_key, ver, 0, 0);
  switchtec_fw_part_summary_free(sum);

  return 0;
}

int pal_get_pax_fw_ver(uint8_t paxid, char *ver)
{
  struct switchtec_fw_part_summary *sum;
  char img_key[MAX_KEY_LEN];
  char bl2_key[MAX_KEY_LEN];

  snprintf(bl2_key, sizeof(bl2_key), "pax%d_bl2", paxid);
  snprintf(img_key, sizeof(img_key), "pax%d_img", paxid);
  if (kv_get(img_key, ver, NULL, 0) == 0)
    return 0;

  if (pal_is_server_off())
    return -1;

  sum = get_pax_ver_sum(paxid);
  if (sum == NULL)
    return -1;

  snprintf(ver, MAX_VALUE_LEN, "%s", sum->bl2.active->version);
  kv_set(bl2_key, ver, 0, 0);
  snprintf(ver, MAX_VALUE_LEN, "%s", sum->img.active->version);
  kv_set(img_key, ver, 0, 0);
  switchtec_fw_part_summary_free(sum);

  return 0;
}

int pal_get_pax_cfg_ver(uint8_t paxid, char *ver)
{
  int fd, ret = 0;
  char device_name[LARGEST_DEVICE_NAME] = {0};
  char cfg_key[MAX_KEY_LEN];
  struct switchtec_dev *dev;
  size_t map_size;
  unsigned int x;
  gasptr_t map;

  snprintf(cfg_key, sizeof(cfg_key), "pax%d_cfg", paxid);
  if (kv_get(cfg_key, ver, NULL, 0) == 0)
    return 0;

  if (pal_is_server_off())
    return -1;

  snprintf(device_name, LARGEST_DEVICE_NAME, SWITCHTEC_DEV, SWITCH_BASE_ADDR + paxid);

  fd = pax_lock();
  if (fd < 0)
    return -1;

  dev = switchtec_open(device_name);
  if (dev == NULL) {
    syslog(LOG_WARNING, "%s: switchtec open %s failed", __func__, device_name);
    pax_unlock(fd);
    return -1;
  }

  map = switchtec_gas_map(dev, 0, &map_size);
  if (map != SWITCHTEC_MAP_FAILED) {
    x = gas_read32(dev, (void __gas*)map + 0x2104);
    switchtec_gas_unmap(dev, map);
  } else {
    ret = -1;
  }

  switchtec_close(dev);
  pax_unlock(fd);

  if (ret == 0) {
    snprintf(ver, MAX_VALUE_LEN, "%x", x);
    kv_set(cfg_key, ver, 0, 0);
  }

  return ret < 0? -1: 0;
}
