LINUX_VERSION_EXTENSION = "-fby2"

COMPATIBLE_MACHINE = "fby2|fbnd|northdome"

FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "file://fbnd.cfg"

KERNEL_MODULE_AUTOLOAD += " \
"
