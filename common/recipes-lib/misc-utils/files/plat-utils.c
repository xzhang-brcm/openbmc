/*
 * Copyright 2019-present Facebook. All Rights Reserved.
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <linux/limits.h>
#include <linux/version.h>

#include "misc-utils.h"

#define	PROC_CPUINFO_FILE	"/proc/cpuinfo"
#define PROC_DT_COMPAT_FILE	"/proc/device-tree/compatible"
#define PROC_LINE_MAX		128

struct cpu_info {
	char model_name[PROC_LINE_MAX];
};

static int proc_read_cpuinfo(struct cpu_info *cinfo)
{
	FILE *fp;
	char buf[PROC_LINE_MAX];

	fp = fopen(PROC_CPUINFO_FILE, "r");
	if (fp == NULL)
		return -1;

	memset(cinfo, 0, sizeof(*cinfo));
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		str_rstrip(buf);
		if (strlen(buf) == 0)
			continue;	/* blank line */

		if (str_startswith(buf, "model name")) {
			char *pos = strstr(buf, ":");
			if (pos == NULL)
				continue;	/* skip invalid line */

			for (pos++; isspace(*pos); pos++) {}
			strncpy(cinfo->model_name, pos,
				sizeof(cinfo->model_name) - 1);
		}
	} /* while */

	fclose(fp);
	return 0;
}

/*
 * Read cpu model information from /proc/cpuinfo.
 *
 * Return:
 *   cpu model.
 */
cpu_model_t get_cpu_model(void)
{
	struct cpu_info cinfo;
	cpu_model_t cpu_model = CPU_MODEL_INVALID;

	if (proc_read_cpuinfo(&cinfo) == 0) {
		if (str_startswith(cinfo.model_name, "ARM926"))
			cpu_model = CPU_MODEL_ARM_V5;
		else if (str_startswith(cinfo.model_name, "ARMv6"))
			cpu_model = CPU_MODEL_ARM_V6;
	}

	return cpu_model;
}

static int proc_read_dt_compat(char *buf, size_t size)
{
	FILE *fp;

	fp = fopen(PROC_DT_COMPAT_FILE, "r");
	if (fp == NULL)
		return -1;

	if (fgets(buf, size, fp) == NULL)
		return -1;

	fclose(fp);
	str_rstrip(buf);
	return 0;
}

/*
 * Read soc model information.
 *
 * Return:
 *   soc model.
 */
soc_model_t get_soc_model(void)
{
	cpu_model_t cpu_model;
	char buf[PROC_LINE_MAX];
	soc_model_t soc_model = SOC_MODEL_INVALID;

	/*
	 * First, let's try to read machine info from device tree.
	 */
	if (proc_read_dt_compat(buf, sizeof(buf)) == 0) {
		if (strstr(buf, "ast2400") != NULL)
			soc_model = SOC_MODEL_ASPEED_G4;
		else if (strstr(buf, "ast2500") != NULL)
			soc_model = SOC_MODEL_ASPEED_G5;

		return soc_model;
	}

	/*
	 * Given device tree information is not available, let's try to
	 * determine the soc model by checking cpu model.
	 * XXX the approach works for now, but it may be broken if new
	 * soc vendors (other than aspeed) are supported.
	 */
	cpu_model = get_cpu_model();
	if (cpu_model == CPU_MODEL_ARM_V5)
		soc_model = SOC_MODEL_ASPEED_G4;
	else if (cpu_model == CPU_MODEL_ARM_V6)
		soc_model = SOC_MODEL_ASPEED_G5;

	return soc_model;
}

/*
 * Read the version of running kernel.
 *
 * Return:
 *   kernel version.
 */
k_version_t get_kernel_version(void)
{
	int i;
	char *pos;
	struct utsname buf;
	unsigned long versions[3] = {0}; /* major.minor.patch */

	if (uname(&buf) != 0) {
		return 0;	/* 0 is an invalid kernel version. */
	}

	i = 0;
	pos = buf.release;
	while (*pos != '\0' && i < ARRAY_SIZE(versions)) {
		if (isdigit(*pos)) {
			versions[i++] = strtol(pos, &pos, 10);
		} else {
			pos++;
		}
	}

	return KERNEL_VERSION(versions[0], versions[1], versions[2]);
}

/*
 * Lock pid file to ensure there is no duplicated instance running.
 *
 * Returns file descriptor if the pid file can be locked; otherwise -1 is
 * returned.
 */
int single_instance_lock(const char *prog_name)
{
	int fd, ret;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "/var/run/%s.pid", prog_name);

	fd = open(path, O_CREAT | O_RDWR, 0666);
	if (fd < 0)
		return -1;

	ret = flock(fd, LOCK_EX | LOCK_NB);
	if (ret < 0) {
		int saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return -1;
	}

	return fd;
}

/*
 * Release the resources acquired by single_instance_lock().
 *
 * Callers don't have to call this function explicitly because all the
 * file descriptors will be closed when the process is terminated.
 *
 * Returns 0 for success, and -1 on failures.
 */
int single_instance_unlock(int lock_fd)
{
	return close(lock_fd);
}
