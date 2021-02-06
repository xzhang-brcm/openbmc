# Copyright 2017-present Facebook. All Rights Reserved.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA

# Intended to compatible with both Python 2.7 and Python 3.x.
from __future__ import absolute_import, division, print_function, unicode_literals

import argparse
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
import textwrap
import time
from glob import glob

from partition import (
    DeviceTreePartition,
    EnvironmentPartition,
    ExternalChecksumPartition,
    LegacyUBootPartition,
    Partition,
)
from virtualcat import ImageFile, MemoryTechnologyDevice, VirtualCat


# keep the timestamp of last healthd restart so we can block and wait at least
# 30 seconds from when it happened before critical operations (to make sure that
# watchdog petting works)
watchdog_timeout = 30.0


# The logging module is absent in old images T25745701.
try:
    import logging
    import logging.handlers

    def add_syslog_handler(logger):
        # type(logging.Logger) -> None
        try:
            run_verbosely(["/etc/init.d/syslog", "start"], logger)
        # Some old init scripts are missing --oknodo
        except subprocess.CalledProcessError:
            pass
        try:
            handler = logging.handlers.SysLogHandler("/dev/log")
            handler.setFormatter(logging.Formatter("pypartition: %(message)s"))
            logger.addHandler(handler)
        except socket.error:
            logger.error("Error initializing syslog; skipping.")

    def get_logger():
        # type: () -> logging.Logger
        logger = logging.getLogger()
        logger.setLevel(logging.INFO)
        handler = logging.StreamHandler()
        handler.setFormatter(
            logging.Formatter("%(levelname)s:%(asctime)-15s %(message)s")
        )
        logger.addHandler(handler)
        if is_openbmc() and not systemd_available(logger):
            add_syslog_handler(logger)
        return logger


except ImportError:

    class StubLogger(object):
        def debug(self, message):
            print(message)

        def info(self, message):
            print(message)

        def warn(self, message):
            print(message)

        def error(self, message):
            print(message)

        def exception(self, message):
            # This doesn't (yet) print the stack trace.
            print(message)

    def add_syslog_handler(logger):
        # type(object) -> None
        pass

    def get_logger():
        # type: () -> object
        logger = StubLogger()
        logger.handlers = []
        return logger


if False:
    from typing import List, Optional, Tuple, Union
    from virtualcat import ImageSourcesType

    LogHandlerType = Union[
        logging.StreamHandler, logging.FileHandler, logging.handlers.SysLogHandler
    ]
    LogDetailsType = List[Tuple[LogHandlerType, logging.Formatter]]
    MTDListType = List[MemoryTechnologyDevice]


def is_openbmc():
    # type: () -> bool
    if os.path.exists("/etc/issue"):
        magics = [b"Open BMC", b"OpenBMC"]
        with open("/etc/issue", "rb") as etc_issue:
            first_line = etc_issue.readline()
        return any(first_line.startswith(magic) for magic in magics)
    return False


def is_wedge100():
    # type: () -> bool
    if os.path.exists("/etc/issue"):
        with open("/etc/issue", "rb") as etc_issue:
            first_line = etc_issue.readline()
        return b" wedge100-" in first_line
    return False


def is_galaxy100():
    # type: () -> bool
    if os.path.exists("/etc/issue"):
        with open("/etc/issue", "rb") as etc_issue:
            first_line = etc_issue.readline()
        return b" galaxy100-" in first_line
    return False


def run_verbosely(command, logger):
    # type: (List[str], logging.Logger) -> None
    command_string = " ".join(command)
    logger.info("Starting to run `{}`.".format(command_string))
    subprocess.check_call(command)
    logger.info("Finished running `{}`.".format(command_string))


def run_verbosely_retry(command, logger):
    # type: (List[str], logging.Logger) -> None
    exception = None

    for _ in range(3):
        try:
            return run_verbosely(command, logger)
        except subprocess.CalledProcessError as e:
            exception = e

        logger.exception(exception)
        time.sleep(30)

    raise exception


def get_checksums_args(description):
    # type: (str) -> Tuple[List[str], argparse.Namespace]
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("image", nargs="?")
    checksum_help = "currently required path to JSON file with dict mapping "
    checksum_help += "md5sums to image descriptions"
    append_help = "append unrecognized checksums to those from CHECKSUMS and "
    append_help += "write the result to this file"
    mtd_labels_help = textwrap.dedent(
        """\
    Name of the MTD device to write to (.e.g, "flash0").  If not
    given pypartition will try to guess the appropriate device, and
    often get it wrong.
    """
    )

    parser.add_argument("--checksums", help=checksum_help, type=argparse.FileType("r"))
    if is_openbmc():
        parser.add_argument("--dry-run", action="store_true")
        parser.add_argument(
            "--force",
            action="store_true",
            help="Flash even if we suspect the image will brick the BMC",
        )
    else:
        parser.add_argument("--serve", action="store_true")
        parser.add_argument("--port", type=int, default=2876)
        parser.add_argument(
            "--append-new-checksums", help=append_help, type=argparse.FileType("w")
        )

    parser.add_argument("--mtd-labels", help=mtd_labels_help)

    args = parser.parse_args()

    if args.checksums:
        checksums = json.load(args.checksums).keys()
    else:
        checksums = []

    return (checksums, args)


# healthd config specifies a mem utlization threshold (e.g. 80%, 95%)
#  that will trigger system reboot,
# Scan healthd config and returns this value
def get_healthd_reboot_threshold():
    # type: () -> [int]
    try:
        with open("/etc/healthd-config.json") as conf:
            d = json.load(conf)
        for t in d["bmc_mem_utilization"]["threshold"]:
            if "reboot" in t["action"]:
                return t["value"]
        # if no reboot threshold found, returns 100%
        return 100
    # If /etc/healthd-config.json does not exist, Python 2 raises plain
    # IOError. Python 3 raises FileNotFoundError but that's a sub-class of
    # IOError.
    except IOError:
        return 100


def flush_tmpfs_logs(logger):
    logfiles = (
        glob("/var/log/messages.*") + glob("/var/log/*.log.*") + glob("/var/log/*.gz")
    )

    for logfile in logfiles:
        if os.path.isfile(logfile):
            logger.info("Removing `{}`".format(logfile))
            os.remove(logfile)

    with open("/var/log/messages", "w") as fd:
        # opening in write mode truncates it anyway, but let's do it two times
        # just to make sure
        fd.truncate()


def exec_bunch(commands, logger):
    # best effort, don't raise if command fail, worst case scenario improve_system
    # will reboot due to not enough memory
    for cmd in commands:
        try:
            run_verbosely(cmd, logger)
        except Exception:
            logger.error("Running `{}` failed".format(" ".join(cmd)))


def systemd_available(logger):
    with open("/proc/1/cmdline") as f:
        c = f.readline()
        return "systemd" in c


def restart_healthd(logger, wait=False, supervisor="sv"):
    if not os.path.exists("/etc/sv/healthd"):
        return

    run_verbosely([supervisor, "restart", "healthd"], logger)

    # healthd is petting watchdog, if something goes wrong and it doesn't do so
    # after restart it may hard-reboot the system - it's better to be safe
    # then sorry here, let's wait 30s before proceeding
    if wait:
        time.sleep(watchdog_timeout)


def restart_services(logger):
    if systemd_available(logger):
        supervisor = "systemctl"
    else:
        supervisor = "sv"
    commands = (
        # restart the high memory profile services
        [supervisor, "restart", "restapi"],
    )
    exec_bunch(commands, logger)

    # similarly to exec_bunch - make the restart best effort, this is not
    # critical
    try:
        restart_healthd(logger, supervisor=supervisor, wait=False)
    except Exception as e:
        logger.error("Restarting healthd failed: {}".format(e))


def drop_caches(logger):
    with open("/proc/sys/vm/drop_caches", "w") as fd:
        fd.write("3")
    # just to give things time to settle
    time.sleep(1)


def get_mem_info():
    # type: () -> [int, int]
    proc_memfree_regex = re.compile("^MemFree: +([0-9]+) kB$", re.MULTILINE)
    proc_memtotal_regex = re.compile("^MemTotal: +([0-9]+) kB$", re.MULTILINE)
    with open("/proc/meminfo", "r") as proc_meminfo:
        meminfo = proc_meminfo.read()
        memTotal = int(proc_memtotal_regex.findall(meminfo)[0])
        memFree = int(proc_memfree_regex.findall(meminfo)[0])
        return [memTotal, memFree]


def is_vboot():
    """
    Vastly simplified method of detecting if running on vboot system
    """
    if os.path.isfile("/usr/local/bin/vboot-util"):
        return True
    return False


def get_vboot_enforcement():
    support = "none"
    if not is_vboot():
        return support
    with open("/proc/mtd", "r") as proc_mtd:
        mtd_info = proc_mtd.read()
    if "romx" not in mtd_info:
        return support
    # vboot-util on Tioga Pass 1 v1.9 (and possibly other versions) is a shell
    # script without a shebang line. Without bash added it raises
    # OSError(errno.NOEXEC, 'Exec format error').
    command = ["/usr/local/bin/vboot-util"]
    with open(command[0], "rb") as vboot_util_binary_or_script:
        if vboot_util_binary_or_script.read(4) != b"\x7fELF":
            command.insert(0, "bash")
    # due to a bug in vboot-util if it use cached data for rom version it may
    # results in trailing garbage and fail during bytes decode, we need to nuke
    # the cache first as a mitigation
    try:
        os.remove("/tmp/cache_store/rom_version")
        os.remove("/tmp/cache_store/rom_uboot_version")
    except Exception:
        # Python2 throws OSError, Python3 FileNotFound - we don't care, it should
        # be ok to leave it as best effort
        pass
    vboot_util_output = subprocess.check_output(command).decode()

    # at this point we already assume that this is vboot enabled system, if
    # only software-enforce is set assume that this is true, if both aren't set
    # though - let's assume that U-Boot is buggy and it's actually
    # hardware-enforced
    if (
        "Flags hardware_enforce:  0x00" in vboot_util_output
        and "Flags software_enforce:  0x01" in vboot_util_output
    ):
        return "software-enforce"
    return "hardware-enforce"


def get_mtds():
    # type: () -> Tuple[MTDListType, MTDListType]
    proc_mtd_regex = re.compile(
        # Device, size, name
        '^(mtd[0-9]+): ([0-9a-f]+) [0-9a-f]+ "([^"]+)"$',
        re.MULTILINE,
    )
    mtd_info = []
    with open("/proc/mtd", "r") as proc_mtd:
        mtd_info = proc_mtd_regex.findall(proc_mtd.read())
    vboot_support = get_vboot_enforcement()
    all_mtds = []
    full_flash_mtds = []
    for (device, size_in_hex, name) in mtd_info:
        if (
            name == "flash"
            or name == "flash0"
            or name == "flash1"
            or name == "Partition_000"
        ):
            if vboot_support == "none" or (
                vboot_support == "software-enforce" and (name in ["flash0", "flash1"])
            ):
                full_flash_mtds.append(
                    MemoryTechnologyDevice(device, int(size_in_hex, 16), name)
                )
            elif vboot_support == "hardware-enforce" and name in ["flash1"]:
                full_flash_mtds.append(
                    MemoryTechnologyDevice(device, int(size_in_hex, 16), name, 384)
                )
        all_mtds.append(MemoryTechnologyDevice(device, int(size_in_hex, 16), name))
    full_flash_mtds.sort(key=lambda mtd: mtd.device_name, reverse=True)
    return (full_flash_mtds, all_mtds)


def get_writeable_mounted_mtds():
    # type: (MTDListType) -> List[Tuple[str, str]]
    writeable_mtd_mounts_regex = re.compile(
        # Device, mountpoint, filesystem, options, dump_freq, fsck_pass
        "^(/dev/mtd(?:block)?[0-9]+) ([^ ]+) [^ ]+ [^ ]*rw[^ ]* [0-9]+ [0-9]+$",
        re.MULTILINE,
    )
    with open("/proc/mounts", "r") as mounts:
        mounts = writeable_mtd_mounts_regex.findall(mounts.read())
    return mounts


def fuser_k_mount_ro(writeable_mounted_mtds, logger):
    # type: (Tuple[str, str], logging.Logger) -> None
    if not writeable_mounted_mtds:
        return

    # logging.shutdown() documentations says "no further use of the logging
    # system should be made after this call", so don't call it but instead
    # selectively remove and add back the specific handlers we expect will stop
    # working if rsyslog is killed because it has a file descriptor like
    # /mnt/data/logfile open.
    for handler in logger.handlers:
        if isinstance(handler, logging.handlers.SysLogHandler):
            handler.close()
            logger.removeHandler(handler)

    for (device, mountpoint) in writeable_mounted_mtds:
        # TODO don't actually fuser and remount on dry run
        try:
            run_verbosely(["fuser", "-km", mountpoint], logger)
        except subprocess.CalledProcessError:
            pass

        # it happens that device is busy just temporarily, worth to try more than
        # once
        run_verbosely_retry(["mount", "-o", "remount,ro", device, mountpoint], logger)

    # rsyslog was likely killed; bring it back
    add_syslog_handler(logger)


def remove_healthd_reboot(logger):
    try:
        with open("/etc/healthd-config.json") as conf:
            d = json.load(conf)
        changed = False
        for t in d["bmc_mem_utilization"]["threshold"]:
            try:
                t["action"].remove("reboot")
                changed = True
            except ValueError:
                pass
        # Yosemite 1 v2.7 (and maybe other images) has a healthd-config.json
        # but is missing a working sv configuration. Instead, healthd is
        # launched from the setup-healthd.sh init script. Additionally, it does
        # not contain the reboot action in its healthd-config.json. So to avoid
        # failing on these systems, only modify the configuration and restart
        # the service on an as-needed basis.
        if changed:
            with open("/etc/healthd-config.json", "w") as conf:
                json.dump(d, conf)
            restart_healthd(logger, wait=True)
    # If /etc/healthd-config.json does not exist, Python 2 raises plain
    # IOError. Python 3 raises FileNotFoundError but that's a sub-class of
    # IOError.
    except IOError:
        pass


def get_kernel_parameters():
    # type: () -> str
    # As far as cov knows, kernel parameters we use are backwards compatible,
    # so it's safe to use the following output even when there are CRC32
    # mismatch warnings.
    return subprocess.check_output(
        ["fw_printenv", "-n", "bootargs"], universal_newlines=True
    ).strip()


def append_to_kernel_parameters(dry_run, addition, logger):
    # type: (bool, str, logging.Logger) -> None
    before = get_kernel_parameters()
    logger.info("Kernel parameters before changes: {}".format(before))
    if "mtdparts" in before:
        logger.info("mtdparts already set in firmware environment.")
    if dry_run:
        logger.info("This is a dry run. Not changing kernel parameters.")
        return
    subprocess.call(["fw_setenv", "bootargs", " ".join([before, addition])])
    logger.info("Kernel parameters after changes: {}".format(get_kernel_parameters()))


def get_partitions(images, checksums, logger):
    # type: (VirtualCat, List[str], logging.Logger) -> List[Partition]
    partitions = []  # type: List[Partition]
    next_magic = images.peek()
    # First 384K is u-boot for legacy or regular-fit images OR
    # the combination of SPL + recovery u-boot. Treat them as the same.
    if next_magic in ExternalChecksumPartition.UBootMagics:
        partitions.append(
            ExternalChecksumPartition(
                0x060000, 0x000000, "u-boot", images, checksums, logger
            )
        )
    else:
        logger.error(
            "Unrecognized magic 0x{:x} at offset 0x{:x}.".format(next_magic, 0)
        )
        sys.exit(1)

    # Env is always in the same location for both legacy and FIT images.
    partitions.append(EnvironmentPartition(0x020000, 0x060000, "env", images, logger))

    # Either we are using the legacy image format or the FIT format.
    next_magic = images.peek()
    if next_magic == LegacyUBootPartition.magic:
        partitions.append(
            LegacyUBootPartition(
                [0x280000, 0x0400000],
                0x080000,
                "kernel",
                images,
                logger,
                LegacyUBootPartition.magic,
            )
        )
        partitions.append(
            LegacyUBootPartition(
                [0xC00000, 0x1780000], partitions[-1].end(), "rootfs", images, logger
            )
        )
    elif next_magic == DeviceTreePartition.magic:
        # The FIT image at 0x80000 could be a u-boot image (size 0x60000)
        # or the kernel+rootfs FIT which is much larger.
        # DeviceTreePartition() will pick the smallest which fits.
        part = DeviceTreePartition(
            [0x60000, 0x1B200000], 0x80000, "fit1", images, logger
        )
        partitions.append(part)

        # If the end of the above partition is 0xE0000 then we need to
        # check a second FIT image. This is definitely the larger one.
        if part.end() == 0xE0000:
            partitions.append(
                DeviceTreePartition([0x1B200000], 0xE0000, "fit2", images, logger)
            )
    else:
        logging.error(
            "Unrecognized magic 0x{:x} at offset 0x{:x}.".format(next_magic, 0x80000)
        )
        sys.exit(1)
    if images.images != []:
        # TODO data0 missing is only okay for ImageFiles, not
        # MemoryTechnologyDevices.  Also, this omits data0 from mtdparts=
        # message.
        partitions.append(
            Partition(
                0x2000000 - partitions[-1].end(),
                partitions[-1].end(),
                "data0",
                images,
                logger,
            )
        )
    return partitions


# The image meta schema refer to:
# meta-facebook/recipes-core/images/image-meta-schema.json
# The image-meta is designed to be a raw image partition in the BMC firmware.
# located at 0x000F_0000, with maximum size 64KB.
# This image-meta partition contains two lines of ASCII strings,
# each ASCII string is a JSON:
#   First line: The image-meta JSON
#   Second line: A simple image-meta-chksum JSON which contain the checksum
#                of image-meta JSON
# Newline (b'\n') is append to both meta and meta-checksum JSON to simplify
# the loading of the JSON objects.
#
# The example image meta partition dumpped as following with json.tool formatted
# to help read:
# strings /dev/mtd3 | while read line; do echo $line | python -m json.tool; done
# {
#     "FBOBMC_IMAGE_META_VER": 1,
#     "version_infos": {
#         "uboot_build_time": "Aug 11 2020 - 22:16:35 +0000",
#         "fw_ver": "fby3vboot2-4f840058283c",
#         "uboot_ver": "2019.04"
#     },
#     "meta_update_action": "Signed",
#     "meta_update_time": "2020-08-11T22:20:40.844432",
#     "part_infos": [
#         {
#             "size": 262144,
#             "type": "rom",
#             "name": "spl",
#             "md5": "602f024562092ac69563f0268ac67265",
#             "offset": 0
#         },
#         {
#             "size": 655360,
#             "type": "raw",
#             "name": "rec-u-boot",
#             "md5": "5036726386d728e1d37f32702a8f3701",
#             "offset": 262144
#         },
#         {
#             "size": 65536,
#             "type": "data",
#             "name": "u-boot-env",
#             "offset": 917504
#         },
#         {
#             "size": 65536,
#             "type": "meta",
#             "name": "image-meta",
#             "offset": 983040
#         },
#         {
#             "num-nodes": 1,
#             "size": 655360,
#             "type": "fit",
#             "name": "u-boot-fit",
#             "offset": 1048576
#         },
#         {
#             "num-nodes": 3,
#             "size": 31850496,
#             "type": "fit",
#             "name": "os-fit",
#             "offset": 1703936
#         }
#     ]
# }
# {
#     "meta_md5": "b5a716b8516b3e6e4abb0ca70a535269"
# }
#
# PS.
#  the python json module will encode(save) the tuple into array,
#  and decode(load) the array as list

FBOBMC_IMAGE_META_LOCATION = 0xF0000
FBOBMC_IMAGE_META_SIZE = 64 * 1024
FBOBMC_IMAGE_META_VER = 1
FBOBMC_PART_INFO_KEY = "part_infos"


class MetaPartitionNotFound(Exception):
    pass


class MetaPartitionCorrupted(Exception):
    pass


class MetaPartitionVerNotSupport(Exception):
    pass


class MetaPartitionMissingPartInfos(Exception):
    pass


def load_image_meta(full_image, logger):
    # type: (ImageSourcesType, logging.Logger) -> dict
    if full_image.size < (FBOBMC_IMAGE_META_LOCATION + FBOBMC_IMAGE_META_SIZE):
        raise MetaPartitionNotFound(
            "image meta is expected locate at 0x{l:08X} with size({s})".format(
                l=FBOBMC_IMAGE_META_LOCATION, s=FBOBMC_IMAGE_META_SIZE
            )
        )

    logger.info("Try loading image meta from full image %s" % full_image)
    len_remain = FBOBMC_IMAGE_META_SIZE
    with open(full_image.file_name, "rb") as fh:
        try:
            fh.seek(FBOBMC_IMAGE_META_LOCATION)
            meta_data = fh.readline(len_remain)
            meta_data_md5 = hashlib.md5(meta_data.strip()).hexdigest()
            len_remain -= len(meta_data)
            meta_data_chksum = fh.readline(len_remain)
            meta_md5 = json.loads(meta_data_chksum.strip())["meta_md5"]
        except Exception as e:
            raise MetaPartitionNotFound(
                "Error while attempting to load meta: {}".format(repr(e))
            )

        if meta_data_md5 != meta_md5:
            raise MetaPartitionCorrupted(
                "Meta partition md5 ({meta_data_md5}) does not match expected md5 {meta_md5}".format(
                    meta_md5=meta_md5, meta_data_md5=meta_data_md5
                )
            )

        meta_info = json.loads(meta_data.strip())
        logger.info(
            "loaded image meta ver(%d) %s at %s with chksum '%s' "
            % (
                meta_info["FBOBMC_IMAGE_META_VER"],
                meta_info["meta_update_action"],
                meta_info["meta_update_time"],
                meta_data_md5,
            )
        )

        if (
            type(meta_info["FBOBMC_IMAGE_META_VER"]) is not int
            or FBOBMC_IMAGE_META_VER < meta_info["FBOBMC_IMAGE_META_VER"]
            or meta_info["FBOBMC_IMAGE_META_VER"] <= 0
        ):
            raise MetaPartitionVerNotSupport(
                "Unsupported meta version {}".format(
                    repr(meta_info["FBOBMC_IMAGE_META_VER"])
                )
            )

        if FBOBMC_PART_INFO_KEY not in meta_info:
            raise MetaPartitionMissingPartInfos(
                "Required metadata entry '{}' not found".format(FBOBMC_PART_INFO_KEY)
            )

        meta_info[FBOBMC_PART_INFO_KEY] = tuple(meta_info[FBOBMC_PART_INFO_KEY])

        return meta_info


def get_partitions_according_meta(full_image, image_meta, logger):
    # type: (ImageSourcesType, List[str], dict, logging.Logger) -> List[Partition]
    logger.info("get partitions according to following image_meta:\n %s" % image_meta)

    partitions = []
    with VirtualCat([full_image]) as vc:
        for part_info in image_meta[FBOBMC_PART_INFO_KEY]:
            partition = None
            if "raw" == part_info["type"]:
                partition = ExternalChecksumPartition(
                    part_info["size"],
                    part_info["offset"],
                    part_info["name"],
                    vc,
                    [part_info["md5"]],
                    logger,
                )
            elif "fit" == part_info["type"]:
                partition = DeviceTreePartition(
                    [part_info["size"]],
                    part_info["offset"],
                    part_info["name"],
                    vc,
                    logger,
                )
            elif "data" == part_info["type"] or "meta" == part_info["type"]:
                partition = Partition(
                    part_info["size"],
                    part_info["offset"],
                    part_info["name"],
                    vc,
                    logger,
                )
            elif "mtdonly" == part_info["type"]:
                if hasattr(full_image, "device_name"):
                    partition = Partition(
                        part_info["size"],
                        part_info["offset"],
                        part_info["name"],
                        vc,
                        logger,
                    )
            elif "rom" == part_info["type"]:
                if get_vboot_enforcement() == "hardware-enforce":
                    partition = Partition(
                        part_info["size"],
                        part_info["offset"],
                        part_info["name"],
                        vc,
                        logger,
                    )
                else:
                    partition = ExternalChecksumPartition(
                        part_info["size"],
                        part_info["offset"],
                        part_info["name"],
                        vc,
                        [part_info["md5"]],
                        logger,
                    )
            else:
                raise AssertionError("Unknown partition %s " % repr(part_info))

            if partition is not None:
                partitions.append(partition)
    return partitions


def get_valid_partitions_according_meta(full_image, image_meta, logger):
    partitions = get_partitions_according_meta(full_image, image_meta, logger)
    logger.info("checked [%s]" % ", ".join(partition.name for partition in partitions))
    for partition in partitions:
        if not partition.valid:
            exiting_msg = "Exiting due to invalid {} partition (details above)."
            logger.error(exiting_msg.format(partition.name))
            sys.exit(1)
    return partitions


def get_valid_partitions(images_or_mtds, checksums, logger):
    # type: (ImageSourcesType, List[str], logging.Logger) -> List[Partition]
    image_meta = None
    if images_or_mtds == []:
        return []
    elif 1 == len(images_or_mtds):
        # image meta based validation only support single full image
        # the case of multiple images_or_mtds, as each is independent partition
        # the legacy code logic can handle
        try:
            image_meta = load_image_meta(images_or_mtds[0], logger)
        except MetaPartitionNotFound as e:
            logger.debug(repr(e))
            logger.info("No image meta found, Validate as legacy format full image")
        except (
            MetaPartitionCorrupted,
            MetaPartitionMissingPartInfos,
            MetaPartitionVerNotSupport,
        ) as e:
            raise e

    if image_meta is not None:
        return get_valid_partitions_according_meta(
            images_or_mtds[0], image_meta, logger
        )

    logger.info(
        "Validating partitions in {}.".format(", ".join(map(str, images_or_mtds)))
    )
    with VirtualCat(images_or_mtds) as vc:
        partitions = get_partitions(vc, checksums, logger)

    # The U-Boot checksum may have been validated while processing the main
    # FIT.
    covered_partitions = []  # type: List[str]
    for image_tree in partitions:
        for covered_partition in image_tree.valid_external_partitions:
            covered_partitions.append(covered_partition)
            logger.info(
                "{} covered by checksum in {}.".format(covered_partition, image_tree)
            )

    # TODO populate valid env partition at build time
    # TODO learn to validate data0 partition
    unvalidated_partitions = ["env", "data0"]  # type: List[str]

    # Ignore invalid checksum for u-boot when in hardware enforcement is
    # currently on. For two reasons:
    # 1) When in hardware-enforcement, the U-Boot partition of the mostly
    #    writable flash1 partition isn't executed during boot. (The only
    #    exception we can think of is a misconfiguration of the watchdog
    #    alternate boot source. It's mostly there just to keep the flash0 and
    #    flash1 images symmetric.
    #
    # 2) get_partitions() doesn't yet properly split the first 84k of SPL/ROM
    #    U-Boot and KEK from the next 300k of Recovery U-Boot. Whitelisting all
    #    of those combinations would be annoying.
    if get_vboot_enforcement() == "hardware-enforce":
        unvalidated_partitions.append("u-boot")

    last = None  # type: Optional[Partition]
    for current in partitions:
        if (
            not current.valid
            and current.name not in covered_partitions
            and current.name not in unvalidated_partitions
        ):
            message = "Exiting due to invalid {} partition (details above)."
            logger.error(message.format(current))
            sys.exit(1)

        # If the partition table has gaps, we need to fix how we generate it,
        # in get_partitions() or elsewhere.
        if last is not None and last.end() != current.partition_offset:
            logger.error("{} does not begin at 0x{:x}".format(current, last.end()))
            sys.exit(1)

        last = current
    return partitions


def other_flasher_running(logger):
    if os.path.exists("/opt/flashy"):
        logger.error("Flashy found: /opt/flashy found in this system")
        return True

    # type: (logging.Logger) -> bool
    basenames = [
        b"autodump.sh",
        b"cpld_upgrade.sh",
        b"dd",
        b"flash_eraseall",
        b"flashcp",
        b"flashrom",
        b"fw-util",
        b"fw_setenv",
        b"improve_system.py",
        b"jbi",
        b"psu-update-bel.py",
        b"psu-update-delta.py",
    ]
    running_flashers = {}

    our_cmdline = [
        "/proc/self/cmdline",
        "/proc/thread-self/cmdline",
        "/proc/{}/cmdline".format(os.getpid()),
    ]

    for cmdline_file in glob("/proc/*/cmdline"):
        if cmdline_file in our_cmdline:
            continue
        try:
            with open(cmdline_file, "rb") as cmdline:
                # Consider all command line parameters so `python foo.py` and
                # `foo.py` are both detected.
                for parameter in cmdline.read().split(b"\x00"):
                    basename = os.path.basename(parameter)
                    if basename in basenames:
                        if basename in running_flashers.keys():
                            running_flashers[basename] += 1
                        else:
                            running_flashers[basename] = 1
        # Processes and their respective files in procfs naturally come and go.
        except IOError:
            pass

    if running_flashers == {}:
        return False

    message = "{} running.".format(b",".join(running_flashers.keys()).decode())
    logger.error(message)
    return True


# Deal with images that have changed names, but are otherwise compatible.
# The version strings are free form, so to come up with regexes that safely
# matches all possible formats would be tough.  Instead, we use this to do
# substitutions before matching in image_file_compatible().
def normalize_version(version):
    # type: (str) -> str
    translations = {"fby2-gpv2-": "fbgp2-"}

    for pattern, patch in translations.items():
        normalized = re.sub(pattern, patch, version, 1, re.I)
        if version != normalized:
            return normalized

    return version


def image_file_compatible(image_file, issue_file, logger):
    # type: (str, str, logging.Logger) -> bool
    logger.info("Checking the sanity of the request")
    with open(issue_file) as f:
        ln = f.readline()

    rx = re.compile(r"^openbmc release (\w+)", re.I)
    m = rx.match(normalize_version(ln))
    if not m:
        # Bad /etc/issue, no way to tell. Failing open. TODO: Should
        # we fail closed?
        return True

    current = m.group(1)

    p = subprocess.Popen(["strings", "-n30", image_file], stdout=subprocess.PIPE)
    p.wait()
    if p.returncode != 0:
        logger.warning("Couldn't check the strings of {}".format(image_file))
        return False

    rx = re.compile(r"U-Boot \d+\.\d+ (\w+)")
    for ln in p.stdout:
        m = rx.search(normalize_version(str(ln)))
        if m:
            new = m.group(1)
            return new == current

    logger.warning("No U-Boot version string found! Assuming it's safe to flash")
    return True


def flash(attempts, image_file, mtd, logger, flash_name, force=False):
    # type: (int, ImageFile, MemoryTechnologyDevice, logging.Logger, Optional[str], bool) -> None
    if image_file.size > mtd.size:
        logger.error("{} is too big for {}.".format(image_file, mtd))
        sys.exit(1)

    if other_flasher_running(logger):
        sys.exit(1)

    image_name = image_file.file_name

    if not image_file_compatible(image_name, "/etc/issue", logger):
        logger.warning("Image {} is not for this platform!".format(image_name))
        if not force:
            logger.error("Aborting flash. Use a valid image or run with --force")
            sys.exit(1)

    logger.info("Proceeding with flash")
    # If MTD has a read-only offset, create a new image file with the
    # readonly offset from the device and the remaining from the image
    # file and use that for flashcp.
    if mtd.offset > 0:
        image_name = image_name + ".tmp"
        with open(image_name, "wb") as out_f:
            with open(mtd.file_name, "rb") as in_f:
                out_f.write(in_f.read(mtd.offset * 1024))
            with open(image_file.file_name, "rb") as in_f:
                in_f.seek(mtd.offset * 1024)
                out_f.write(in_f.read())

    # TODO only write bytes that have changed
    flash_command = ["flashcp", image_name, mtd.file_name]
    if attempts < 1:
        flash_command = ["dd", "if={}".format(image_file.file_name), "of=/dev/null"]
        attempts = 1

    for attempt in range(attempts):
        try:
            run_verbosely(flash_command, logger)
            break
        except subprocess.CalledProcessError as result:
            # Retry the specified amount but even on consecutive failures don't
            # exit yet. Instead let the verification stage after this determine
            # the next steps.
            logger.warning(
                "flashcp attempt {} returned {}.".format(attempt, result.returncode)
            )
    # Remove temp file.
    if image_file.file_name != image_name:
        os.remove(image_name)


# Unfortunately there is no mutual exclusion between flashing and rebooting.
# So for now minimize the window of opportunity for those operations to occur
# concurrently by rebooting as soon as possible after verification.
def reboot(dry_run, reason, logger):
    # type: (bool, str, logging.Logger) -> None

    if other_flasher_running(logger):
        sys.exit(1)

    logger.info(reason)

    reboot_command = ["shutdown", "-r", "now", "pypartition is {}.".format(reason)]
    if dry_run:
        reboot_command = [
            "wall",
            "pypartition would be {} if this were not a dry run.".format(reason),
        ]
        logger.info("This is a dry run. Not rebooting.")
    else:
        logger.info("Proceeding with reboot.")

    # The logging module is absent in old images T25745701.
    try:
        logging.shutdown()
    except NameError:
        pass

    # Trying to run anything after the `shutdown -r` command is issued would be
    # racing against shutdown killing this process.
    if subprocess.call(reboot_command) == 0:
        sys.exit(0)
    else:
        print("Unable to reboot.", file=sys.stderr)
        sys.exit(1)
