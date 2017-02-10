# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE_TYPE := driver

MODULE_STATIC_LIBS := ulib/ddk ulib/hexdump

MODULE_LIBS := ulib/driver ulib/magenta ulib/musl

MODULE := usb-ethernet-ax88772b

MODULE_SRCS := $(LOCAL_DIR)/asix-88772b.c

include make/module.mk


MODULE_TYPE := driver

MODULE_STATIC_LIBS := ulib/ddk ulib/hexdump

MODULE_LIBS := ulib/driver ulib/magenta ulib/musl

MODULE := usb-ethernet-ax88179

MODULE_SRCS := $(LOCAL_DIR)/asix-88179.c

include make/module.mk

MODULE_TYPE := driver

MODULE_STATIC_LIBS := ulib/ddk ulib/hexdump

MODULE_LIBS := ulib/driver ulib/magenta ulib/musl ulib/mxio

MODULE := usb-ethernet-lan9514

MODULE_SRCS := $(LOCAL_DIR)/smsc-lan9514.c

include make/module.mk
