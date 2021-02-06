# Copyright 2015-present Facebook. All Rights Reserved.
SUMMARY = "Sensor History"
DESCRIPTION = "Util for displaying the detailed sensor history"
SECTION = "base"
PR = "r1"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://sensor-history.c;beginline=4;endline=16;md5=b395943ba8a0717a83e62ca123a8d238"

SRC_URI = "file://Makefile \
           file://sensor-history.c \
          "
S = "${WORKDIR}"

binfiles = "sensor-history"

DEPENDS =+ "libpal"
RDEPENDS_${PN} =+ "libpal"

pkgdir = "sensor-util"

do_install() {
  dst="${D}/usr/local/fbpackages/${pkgdir}"
  bin="${D}/usr/local/bin"
  install -d $dst
  install -d $bin
  install -m 755 sensor-history ${dst}/sensor-history
  ln -snf ../fbpackages/${pkgdir}/sensor-history ${bin}/sensor-history
}

FBPACKAGEDIR = "${prefix}/local/fbpackages"

FILES_${PN} = "${FBPACKAGEDIR}/sensor-util ${prefix}/local/bin"
