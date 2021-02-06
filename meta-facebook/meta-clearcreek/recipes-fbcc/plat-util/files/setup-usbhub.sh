#!/bin/bash
#
# Copyright 2020-present Facebook. All Rights Reserved.
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
#

i2cset -f -y 17 0x2c 0x00 0x24 0x04 0x14 0x25 0x00 0x02 0xBB 0x20 0x02 0x00 0x00 0x00 0x01 0x32 0x01 0x32 0x32 s
i2cset -f -y 17 0x2c 0xff 0x01 s
i2cset -f -y 19 0x2c 0x00 0x24 0x04 0x14 0x25 0x00 0x02 0xBB 0x20 0x02 0x00 0x00 0x00 0x01 0x32 0x01 0x32 0x32 s
i2cset -f -y 19 0x2c 0xff 0x01 s
i2cset -f -y 20 0x2c 0x00 0x24 0x04 0x14 0x25 0x00 0x02 0xBB 0x20 0x02 0x00 0x00 0x00 0x01 0x32 0x01 0x32 0x32 s
i2cset -f -y 20 0x2c 0xff 0x01 s