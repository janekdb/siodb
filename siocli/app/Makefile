# Copyright (C) 2019-2020 Siodb GmbH. All rights reserved.
# Use of this source code is governed by a license that can be found
# in the LICENSE file.

# siocli application makefile

SRC_DIR:=$(dir $(realpath $(firstword $(MAKEFILE_LIST))))
include ../../mk/Prolog.mk

TARGET_EXE:=siocli

CXX_SRC:=SiocliMain.cpp

TARGET_OWN_LIBS:=siocli

TARGET_COMMON_LIBS:=crypto options data net proto protobuf io sys utils stl_ext crt_ext

TARGET_LIBS:=-lboost_filesystem -lboost_program_options -lboost_system -lprotobuf -lcrypto -lssl

include $(MK)/Main.mk
