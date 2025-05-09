# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2013 - 2023 calmwu

#####################
# Helpful functions #
#####################

# {?}是函数参数的位置，${1}是第一个参数，${2}是第二个参数，以此类推

readlink = $(shell readlink -f ${1})

# helper functions for converting kernel version to version codes
get_kver = $(or $(word ${2},$(subst ., ,${1})),0)
get_kvercode = $(shell [ "${1}" -ge 0 -a "${1}" -le 255 2>/dev/null ] && \
                       [ "${2}" -ge 0 -a "${2}" -le 255 2>/dev/null ] && \
                       [ "${3}" -ge 0 -a "${3}" -le 255 2>/dev/null ] && \
                       printf %d $$(( ( ${1} << 16 ) + ( ${2} << 8 ) + ( ${3} ) )) )

################
# depmod Macro #
################

cmd_depmod = /sbin/depmod $(if ${SYSTEM_MAP_FILE},-e -F ${SYSTEM_MAP_FILE}) \
                          $(if $(strip ${INSTALL_MOD_PATH}),-b ${INSTALL_MOD_PATH}) \
                          -a ${KVER}

################
# dracut Macro #
################

cmd_initrd := $(shell \
                if which dracut > /dev/null 2>&1 ; then \
                    echo "dracut --force"; \
                elif which update-initramfs > /dev/null 2>&1 ; then \
                    echo "update-initramfs -u"; \
                fi )

#####################
# Environment tests #
#####################
# 如果没有指定BUILD_KERNEL，则使用uname -r的值
ifeq (,${BUILD_KERNEL})
BUILD_KERNEL=$(shell uname -r)
endif

# Kernel Search Path
# All the places we look for kernel source
KSP :=  /lib/modules/${BUILD_KERNEL}/source \
        /lib/modules/${BUILD_KERNEL}/build \
        /usr/src/linux-${BUILD_KERNEL} \
        /usr/src/linux-$(${BUILD_KERNEL} | sed 's/-.*//') \
        /usr/src/kernel-headers-${BUILD_KERNEL} \
        /usr/src/kernel-source-${BUILD_KERNEL} \
        /usr/src/linux-$(${BUILD_KERNEL} | sed 's/\([0-9]*\.[0-9]*\)\..*/\1/') \
        /usr/src/linux \
        /usr/src/kernels/${BUILD_KERNEL} \
        /usr/src/kernels

test_dir = $(shell [ -e ${dir}/include/linux ] && echo ${dir})
KSP := $(foreach dir, ${KSP}, ${test_dir})

# we will use this first valid entry in the search path
# 内核源码的目录
ifeq (,${KSRC})
  KSRC := $(firstword ${KSP})
endif

ifeq (,${KSRC})
  $(warning *** Kernel header files not in any of the expected locations.)
  $(warning *** Install the appropriate kernel development package, e.g.)
  $(error kernel-devel, for building kernel modules and try again)
else
ifeq (/lib/modules/${BUILD_KERNEL}/source, ${KSRC})
  KOBJ :=  /lib/modules/${BUILD_KERNEL}/build
else
  KOBJ :=  ${KSRC}
endif
endif

SCRIPT_PATH := ${KSRC}/scripts
info_signed_modules =

ifeq (,${SCRIPT_PATH})
  info_signed_modules += echo "*** Could not find sign-file script. Cannot sign driver." ;
else
  SIGN_FILE_EXISTS := $(or $(and $(wildcard $(SCRIPT_PATH)/sign-file),1),)
  PRIV_KEY_EXISTS := $(or $(and $(wildcard intel-linux-key.key),1),)
  PUB_KEY_EXISTS := $(or $(and $(wildcard intel-linux-key.crt),1),)
ifneq ($(and $(SIGN_FILE_EXISTS),$(PRIV_KEY_EXISTS),$(PUB_KEY_EXISTS)),)
  info_signed_modules += \
    echo "*** Is sign-file present: ${SIGN_FILE_EXISTS}" ; \
    echo "*** Is private key present: ${PRIV_KEY_EXISTS}" ; \
    echo "*** Is public key present: ${PUB_KEY_EXISTS}" ;
  info_signed_modules += echo "*** All files are present, signing driver." ;
  sign_driver = $(shell ${SCRIPT_PATH}/sign-file sha256 intel-linux-key.key \
                        intel-linux-key.crt i40e.ko)
else
  info_signed_modules += echo "*** Files are missing, cannot sign driver." ;
  sign_driver =
endif
endif

# Version file Search Path
VSP :=  ${KOBJ}/include/generated/utsrelease.h \
        ${KOBJ}/include/linux/utsrelease.h \
        ${KOBJ}/include/linux/version.h \
        ${KOBJ}/include/generated/uapi/linux/version.h \
        /boot/vmlinuz.version.h

# Config file Search Path
CSP :=  ${KOBJ}/include/generated/autoconf.h \
        ${KOBJ}/include/linux/autoconf.h \
        /boot/vmlinuz.autoconf.h

# System.map Search Path (for depmod)
MSP := ${KSRC}/System.map \
       /usr/lib/debug/boot/System.map-${BUILD_KERNEL} \
       /boot/System.map-${BUILD_KERNEL}

# prune the lists down to only files that exist 对列表中的文件进行删减，只保留存在的文件
test_file = $(shell [ -f ${1} ] && echo ${1})
VSP := $(foreach file, ${VSP}, $(call test_file,${file}))
CSP := $(foreach file, ${CSP}, $(call test_file,${file}))
MSP := $(foreach file, ${MSP}, $(call test_file,${file}))

# and use the first valid entry in the Search Paths
ifeq (,${VERSION_FILE})
  VERSION_FILE := $(firstword ${VSP})
endif

ifeq (,${CONFIG_FILE})
  CONFIG_FILE := $(firstword ${CSP})
endif

ifeq (,${SYSTEM_MAP_FILE})
  SYSTEM_MAP_FILE := $(firstword ${MSP})
endif

ifeq (,$(wildcard ${VERSION_FILE}))
  $(error Linux kernel source not configured - missing version header file)
endif

ifeq (,$(wildcard ${CONFIG_FILE}))
  $(error Linux kernel source not configured - missing autoconf.h)
endif

ifeq (,$(wildcard ${SYSTEM_MAP_FILE}))
  $(warning Missing System.map file - depmod will not check for missing symbols during module installation)
endif

ifneq ($(words $(subst :, ,$(CURDIR))), 1)
  $(error Sources directory '$(CURDIR)' cannot contain spaces nor colons. Rename directory or move sources to another path)
endif

########################
# Extract config value #
########################

get_config_value = $(shell ${CC} -E -dM ${CONFIG_FILE} 2> /dev/null |\
                           grep -m 1 ${1} | awk '{ print $$3 }')

                           ########################
# Check module signing #
########################

CONFIG_MODULE_SIG_ALL := $(call get_config_value,CONFIG_MODULE_SIG_ALL)
CONFIG_MODULE_SIG_FORCE := $(call get_config_value,CONFIG_MODULE_SIG_FORCE)
CONFIG_MODULE_SIG_KEY := $(call get_config_value,CONFIG_MODULE_SIG_KEY)

SIG_KEY_SP := ${KOBJ}/${CONFIG_MODULE_SIG_KEY} \
              ${KOBJ}/certs/signing_key.pem

SIG_KEY_FILE := $(firstword $(foreach file, ${SIG_KEY_SP}, $(call test_file,${file})))

# print a warning if the kernel configuration attempts to sign modules but
# the signing key can't be found.
ifneq (${SIG_KEY_FILE},)
warn_signed_modules := : ;
else
warn_signed_modules :=
ifeq (${CONFIG_MODULE_SIG_ALL},1)
warn_signed_modules += \
    echo "*** The target kernel has CONFIG_MODULE_SIG_ALL enabled, but" ; \
    echo "*** the signing key cannot be found. Module signing has been" ; \
    echo "*** disabled for this build." ;
endif # CONFIG_MODULE_SIG_ALL=y
ifeq (${CONFIG_MODULE_SIG_FORCE},1)
    echo "warning: The target kernel has CONFIG_MODULE_SIG_FORCE enabled," ; \
    echo "warning: but the signing key cannot be found. The module must" ; \
    echo "warning: be signed manually using 'scripts/sign-file'." ;
endif # CONFIG_MODULE_SIG_FORCE
DISABLE_MODULE_SIGNING := Yes
endif

#######################
# Linux Version Setup #
#######################

# The following command line parameter is intended for development of KCOMPAT
# against upstream kernels such as net-next which have broken or non-updated
# version codes in their Makefile. They are intended for debugging and
# development purpose only so that we can easily test new KCOMPAT early. If you
# don't know what this means, you do not need to set this flag. There is no
# arcane magic here.

# Convert LINUX_VERSION into LINUX_VERSION_CODE
ifneq (${LINUX_VERSION},)
  LINUX_VERSION_CODE=$(call get_kvercode,$(call get_kver,${LINUX_VERSION},1),$(call get_kver,${LINUX_VERSION},2),$(call get_kver,${LINUX_VERSION},3))
endif

# Honor LINUX_VERSION_CODE
ifneq (${LINUX_VERSION_CODE},)
  $(warning Forcing target kernel to build with LINUX_VERSION_CODE of ${LINUX_VERSION_CODE}$(if ${LINUX_VERSION}, from LINUX_VERSION=${LINUX_VERSION}). Do this at your own risk.)
  KVER_CODE := ${LINUX_VERSION_CODE}
  EXTRA_CFLAGS += -DLINUX_VERSION_CODE=${LINUX_VERSION_CODE}
endif

EXTRA_CFLAGS += ${CFLAGS_EXTRA}

# get the kernel version - we use this to find the correct install path
KVER := $(shell ${CC} ${EXTRA_CFLAGS} -E -dM ${VERSION_FILE} | grep UTS_RELEASE | \
        awk '{ print $$3 }' | sed 's/\"//g')

# assume source symlink is the same as build, otherwise adjust KOBJ
ifneq (,$(wildcard /lib/modules/${KVER}/build))
  ifneq (${KSRC},$(call readlink,/lib/modules/${KVER}/build))
    KOBJ=/lib/modules/${KVER}/build
  endif
endif

ifeq (${KVER_CODE},)
  KVER_CODE := $(shell ${CC} ${EXTRA_CFLAGS} -E -dM ${VSP} 2> /dev/null |\
                 grep -m 1 LINUX_VERSION_CODE | awk '{ print $$3 }' | sed 's/\"//g')
endif

# minimum_kver_check
#
# helper function to provide uniform output for different drivers to abort the
# build based on kernel version check. Usage: "$(call minimum_kver_check,2,6,XX)".
define _minimum_kver_check
ifeq (0,$(shell [ ${KVER_CODE} -lt $(call get_kvercode,${1},${2},${3}) ]; echo "$$?"))
  $$(warning *** Aborting the build.)
  $$(error This driver is not supported on kernel versions older than ${1}.${2}.${3})
endif
endef
minimum_kver_check = $(eval $(call _minimum_kver_check,${1},${2},${3}))

####################
# CCFLAGS variable #
####################

# set correct CCFLAGS variable for kernels older than 2.6.24
ifeq (0,$(shell [ ${KVER_CODE} -lt $(call get_kvercode,2,6,24) ]; echo $$?))
CCFLAGS_VAR := EXTRA_CFLAGS
else
CCFLAGS_VAR := ccflags-y
endif

#################
# KBUILD_OUTPUT #
#################

# Only set KBUILD_OUTPUT if the real paths of KOBJ and KSRC differ
ifneq ($(call readlink,${KSRC}),$(call readlink,${KOBJ}))
export KBUILD_OUTPUT ?= ${KOBJ}
endif

############################
# Module Install Directory #
############################

# Default to using updates/drivers/net/ethernet/intel/ path, since depmod since
# v3.1 defaults to checking updates folder first, and only checking kernels/
# and extra afterwards. We use updates instead of kernel/* due to desire to
# prevent over-writing built-in modules files.
export INSTALL_MOD_DIR ?= extra/calmwu_modules/${DRIVER}

######################
# Kernel Build Macro #
######################

# kernel build function
# ${1} is the kernel build target
# ${2} may contain any extra rules to pass directly to the sub-make process
#
# This function is expected to be executed by
#   @+$(call kernelbuild,<target>,<extra parameters>)
# from within a Makefile recipe.
#
# The following variables are expected to be defined for its use:
# GCC_I_SYS -- if set it will enable use of gcc-i-sys.sh wrapper to use -isystem
# CCFLAGS_VAR -- the CCFLAGS variable to set extra CFLAGS
# EXTRA_CFLAGS -- a set of extra CFLAGS to pass into the ccflags-y variable
# KSRC -- the location of the kernel source tree to build against
# DRIVER_UPPERCASE -- the uppercase name of the kernel module, set from DRIVER
# W -- if set, enables the W= kernel warnings options
# C -- if set, enables the C= kernel sparse build options
#
kernelbuild = $(call warn_signed_modules) \
              ${MAKE} $(if ${GCC_I_SYS},CC="${GCC_I_SYS}") \
                      ${CCFLAGS_VAR}="${EXTRA_CFLAGS}" \
                      -C "${KSRC}" \
                      CONFIG_${DRIVER_UPPERCASE}=m \
                      $(if ${DISABLE_MODULE_SIGNING},CONFIG_MODULE_SIG=n) \
                      $(if ${DISABLE_MODULE_SIGNING},CONFIG_MODULE_SIG_ALL=) \
                      M="${CURDIR}" \
                      $(if ${W},W="${W}") \
                      $(if ${C},C="${C}") \
                      ${2} ${1}