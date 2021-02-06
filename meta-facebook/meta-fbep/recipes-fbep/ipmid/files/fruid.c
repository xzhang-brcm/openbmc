/*
 *
 * Copyright 2015-present Facebook. All Rights Reserved.
 *
 * This file provides platform specific implementation of FRUID information
 *
 * FRUID specification can be found at
 * www.intel.com/content/dam/www/public/us/en/documents/product-briefs/platform-management-fru-document-rev-1-2-feb-2013.pdf
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
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <openbmc/pal.h>
#include <facebook/asic.h>
#include "fruid.h"

#define FRUID_SIZE        512
/*
 * copy_eeprom_to_bin - copy the eeprom to binary file im /tmp directory
 *
 * @eeprom_file   : path for the eeprom of the device
 * @bin_file      : path for the binary file
 *
 * returns 0 on successful copy
 * returns non-zero on file operation errors
 */
int copy_eeprom_to_bin(const char * eeprom_file, const char * bin_file) {

  int eeprom;
  int bin;
  uint64_t tmp[FRUID_SIZE];
  ssize_t bytes_rd, bytes_wr;

  errno = 0;

  eeprom = open(eeprom_file, O_RDONLY);
  if (eeprom == -1) {
    syslog(LOG_ERR, "%s: unable to open the %s file: %s",
	__func__, eeprom_file, strerror(errno));
    return errno;
  }

  bin = open(bin_file, O_WRONLY | O_CREAT, 0644);
  if (bin == -1) {
    syslog(LOG_ERR, "%s: unable to create %s file: %s",
	__func__, bin_file, strerror(errno));
    goto err;
  }

  bytes_rd = read(eeprom, tmp, FRUID_SIZE);
  if (bytes_rd < 0) {
    syslog(LOG_ERR, "%s: read %s file failed: %s",
	__func__, eeprom_file, strerror(errno));
    goto exit;
  } else if (bytes_rd < FRUID_SIZE) {
    syslog(LOG_ERR, "%s: less than %d bytes", __func__, FRUID_SIZE);
    goto exit;
  }

  bytes_wr = write(bin, tmp, bytes_rd);
  if (bytes_wr != bytes_rd) {
    syslog(LOG_ERR, "%s: write to %s file failed: %s",
	__func__, bin_file, strerror(errno));
    goto exit;
  }

exit:
  close(bin);
err:
  close(eeprom);

  return errno;
}

/* Populate the platform specific eeprom for fruid info */
int plat_fruid_init(void)
{
  char path[64] = {0};

  if (copy_eeprom_to_bin(MB_EEPROM, MB_BIN))
    syslog(LOG_WARNING, "[%s]Copy EEPROM to %s Failed",__func__, MB_BIN);

  if (copy_eeprom_to_bin(PDB_EEPROM, PDB_BIN))
    syslog(LOG_WARNING, "[%s]Copy EEPROM to %s Failed",__func__, PDB_BIN);

  pal_get_fruid_eeprom_path(FRU_BSM, path);
  if (copy_eeprom_to_bin(path, BSM_BIN))
    syslog(LOG_WARNING, "[%s]Copy EEPROM to %s Failed",__func__, BSM_BIN);

  return 0;
}

int plat_fruid_size(unsigned char payload_id)
{
  struct stat buf;
  int ret;

  // check the size of the file and return size
  ret = stat(MB_BIN, &buf);
  if (ret) {
    return 0;
  }

  return buf.st_size;
}

int plat_fruid_data(unsigned char payload_id, int fru_id, int offset, int count, unsigned char *data) {
  int fd;
  int ret;
  char fru_dev[LARGEST_DEVICE_NAME] = {0};

  // Align wiht IPMI FRU ID, 0-based
  if (fru_id == FRU_MB-1)
    snprintf(fru_dev, LARGEST_DEVICE_NAME, MB_BIN);
  else if (fru_id == FRU_PDB-1)
    snprintf(fru_dev, LARGEST_DEVICE_NAME, PDB_BIN);
  else if (fru_id == FRU_BSM-1)
    snprintf(fru_dev, LARGEST_DEVICE_NAME, BSM_BIN);
  else
    return -1;

  // open file for read purpose
  fd = open(fru_dev, O_RDONLY);
  if (fd < 0) {
    return fd;
  }

  // seek position based on given offset
  ret = lseek(fd, offset, SEEK_SET);
  if (ret < 0) {
    close(fd);
    return ret;
  }

  // read the file content
  ret = read(fd, data, count);
  if (ret != count) {
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}
