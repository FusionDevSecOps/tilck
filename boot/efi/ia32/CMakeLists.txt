# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.2)

set(EFI_ARCH ia32)
set(GNUEFI_DIR ${TCROOT}/i386/gnu-efi)
set_cross_compiler()

file(GLOB SOURCES "../*.c")
include (../GenericBuild.cmake)
