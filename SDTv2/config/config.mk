#
# Copyright     This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
#               If a copy of the MPL was not distributed with this file, 
#               You can obtain one at http://mozilla.org/MPL/2.0/.
#               Copyright Alstom Transport SA or its subsidiaries and others, 2010-2022. All rights reserved.
# 
# Component     SDTv2 Library
#
# File          LINUX_cfg
#
# Requirements  NA
#
# Abstract      Build definitions for x86 Linux
#

ARCH      = linux
CPU       = cpu
TARGET_OS = LINUX
TOOLCHAIN =

SDTLIBFEATURES = -DSDT_ENABLE_IPT \
                 -DSDT_ENABLE_MVB \
                 -DSDT_ENABLE_WTB \
                 -DSDT_SECURE 
LINT_SYSINCLUDE_DIRECTIVES = -i /usr/include +libh stdint.h +libh pthread.h                  
# Remove LDFLAGS += -m32 (no 32-bit specific flags needed)
PLATFORM_CPPFLAGS = -DO_LE $(SDTLIBFEATURES)
# Remove PLATFORM_CFLAGS+=-m32 (no 32-bit restriction)
# Remove PLATFORM_CPPFLAGS+=-m32 (no 32-bit restriction)
# Remove PLATFORM_LDFLAGS+=$(LDFLAGS) (no 32-bit specific linker flags)