# Copyright (C) 2019-2020 Siodb GmbH. All rights reserved.
# Use of this source code is governed by a license that can be found
# in the LICENSE file.

# Makefile for the common library "config"

SRC_DIR:=$(dir $(realpath $(firstword $(MAKEFILE_LIST))))
include ../../../../../mk/Prolog.mk

TARGET_LIB:=siodb_common_config

C_HDR:= \
	CompilerDefs.h  \
	Config.h  \
	SiodbVersion.h

CXX_HDR:= \
	SiodbDefs.h

include $(MK)/Main.mk
