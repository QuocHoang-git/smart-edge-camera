# ==============================================================================
# CMake Toolchain file for Rockchip RV1103/RV1106 (Luckfox Pico)
# Target Architecture : ARM Cortex-A7 (armhf)
# C Library           : uClibc-ng
# Toolchain Prefix    : arm-rockchip830-linux-uclibcgnueabihf
# ==============================================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Define SDK & Toolchain Root
set(LUCKFOX_SDK_PATH "/home/userver/1_Workspace/97_LinuxCustom/luckfox-pico" CACHE PATH "Path to Luckfox Pico SDK")
set(TOOLCHAIN_ROOT "${LUCKFOX_SDK_PATH}/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf")
set(TOOLCHAIN_BIN "${TOOLCHAIN_ROOT}/bin")
set(TOOLCHAIN_SYSROOT "${TOOLCHAIN_ROOT}/arm-rockchip830-linux-uclibcgnueabihf/sysroot")

# Cross-compilers
set(CMAKE_C_COMPILER "${TOOLCHAIN_BIN}/arm-rockchip830-linux-uclibcgnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN}/arm-rockchip830-linux-uclibcgnueabihf-g++")
set(CMAKE_STRIP "${TOOLCHAIN_BIN}/arm-rockchip830-linux-uclibcgnueabihf-strip" CACHE FILEPATH "Strip tool")

# Sysroot configuration
set(CMAKE_SYSROOT "${TOOLCHAIN_SYSROOT}")

# Hardware floating point flags for ARM Cortex-A7 (VFPv4-D16 / NEON)
set(ARM_ARCH_FLAGS "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT "${ARM_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARM_ARCH_FLAGS}")

# Search mode settings: search programs only in host, libraries/headers in target sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
