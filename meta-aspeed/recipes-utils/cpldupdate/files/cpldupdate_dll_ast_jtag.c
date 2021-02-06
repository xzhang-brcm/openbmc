/*
 * Copyright 2016-present Facebook. All Rights Reserved.
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/version.h>
#include <openbmc/log.h>
#include <openbmc/misc-utils.h>

#include <openbmc/cpldupdate_dll.h>

#ifndef JTAG_SYSFS_DIR
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,1,51)
#define JTAG_SYSFS_DIR "/sys/devices/platform/ast-jtag.0/"
#else
#define JTAG_SYSFS_DIR "/sys/devices/platform/ahb/ahb:apb/1e6e4000.jtag/"
#endif
#endif

#ifndef JTAG_CHAR_DEV
#define JTAG_CHAR_DEV "/dev/jtag0"
#endif

#define JTAG_SYSFS_TDI JTAG_SYSFS_DIR "tdi"
#define JTAG_SYSFS_TDO JTAG_SYSFS_DIR "tdo"
#define JTAG_SYSFS_TMS JTAG_SYSFS_DIR "tms"
#define JTAG_SYSFS_TCK JTAG_SYSFS_DIR "tck"

struct jtag_ctx {
  int fd_chardev;
  int fd[CPLDUPDATE_PIN_MAX];
};

void cpldupdate_dll_free(void *ctx) {
  struct jtag_ctx *jctx = (struct jtag_ctx *)ctx;

  if (jctx != NULL) {
    int i;

    if (jctx->fd_chardev >= 0) {
      close(jctx->fd_chardev);
    }

    for (i = 0; i < CPLDUPDATE_PIN_MAX; i++) {
      if (jctx->fd[i] != -1) {
        close(jctx->fd[i]);
      }
    }

    free(jctx);
  }
}

int cpldupdate_dll_init(int argc, const char *const argv[], void **ctx) {
  int rc = 0;
  int i = 0;
  struct jtag_ctx *new_ctx = NULL;
  new_ctx = malloc(sizeof(*new_ctx));
  if (!new_ctx) {
    return ENOMEM;
  }
  new_ctx->fd_chardev = -1;
  for (i = 0; i < CPLDUPDATE_PIN_MAX; new_ctx->fd[i++] = -1);

  /* Kernel 5.6 JTAG driver default let JTAG controller in slave mode,
   * and only enable master mode when JTAG dev is opened.
   * So add open() here for compatibility.
   */
  if(get_kernel_version() >= KERNEL_VERSION(5, 6, 0)) {
    new_ctx->fd_chardev = open(JTAG_CHAR_DEV, O_RDWR);
    if (new_ctx->fd_chardev < 0) {
      OBMC_ERROR(errno, "failed to open %s\n", JTAG_CHAR_DEV);
      goto err_out;
    }
  }

  /* open files */
#define OPEN_AND_CHECK(fd, dir, mode) do {      \
  rc = open(dir, mode);                         \
  if (rc == -1) {                               \
    rc = errno;                                 \
    goto err_out;                               \
  }                                             \
  fd = rc;                                      \
} while(0)

  OPEN_AND_CHECK(new_ctx->fd[CPLDUPDATE_PIN_TDI], JTAG_SYSFS_TDI, O_WRONLY);
  OPEN_AND_CHECK(new_ctx->fd[CPLDUPDATE_PIN_TCK], JTAG_SYSFS_TCK, O_WRONLY);
  OPEN_AND_CHECK(new_ctx->fd[CPLDUPDATE_PIN_TMS], JTAG_SYSFS_TMS, O_WRONLY);
  OPEN_AND_CHECK(new_ctx->fd[CPLDUPDATE_PIN_TDO], JTAG_SYSFS_TDO, O_RDONLY);

#undef OPEN_AND_CHECK

  *ctx = new_ctx;
  return 0;

 err_out:
  cpldupdate_dll_free(new_ctx);
  return rc;
}

int cpldupdate_dll_write_pin(void *ctx, cpldupdate_pin_en pin,
                             cpldupdate_pin_value_en value) {
  struct jtag_ctx *jctx = (struct jtag_ctx *)ctx;

  if (!jctx || pin >= CPLDUPDATE_PIN_MAX) {
    return EINVAL;
  }

  if (lseek(jctx->fd[pin], 0, SEEK_SET) == (off_t)-1) {
    return errno;
  }
  if (write(jctx->fd[pin],
            (value == CPLDUPDATE_PIN_VALUE_LOW) ? "0" : "1", 1) < 0) {
    return errno;
  }

  return 0;
}

int cpldupdate_dll_read_pin(void *ctx, cpldupdate_pin_en pin,
                            cpldupdate_pin_value_en *value) {
  struct jtag_ctx *jctx = (struct jtag_ctx *)ctx;
  char buf[32] = {0};

  if (!jctx || pin >= CPLDUPDATE_PIN_MAX) {
    return EINVAL;
  }

  if (lseek(jctx->fd[pin], 0, SEEK_SET) == (off_t)-1) {
    return errno;
  }
  if (read(jctx->fd[pin], buf, sizeof(buf)) < 0) {
    return errno;
  }
  *value = atoi(buf) ? CPLDUPDATE_PIN_VALUE_HIGH : CPLDUPDATE_PIN_VALUE_LOW;

  return 0;
}
