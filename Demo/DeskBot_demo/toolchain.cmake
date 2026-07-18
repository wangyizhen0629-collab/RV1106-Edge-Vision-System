# Specify the cross-compilation toolchain
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

get_filename_component(ECHO_MATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(RV1106_SDK_PATH "${ECHO_MATE_ROOT}/SDK/rv1106-sdk"
    CACHE PATH "Path to the RV1106 SDK")

set(RV1106_TOOLCHAIN_BIN
    "${RV1106_SDK_PATH}/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin")

# Specify the compiler paths
set(CMAKE_C_COMPILER
    "${RV1106_TOOLCHAIN_BIN}/arm-rockchip830-linux-uclibcgnueabihf-gcc")
set(CMAKE_CXX_COMPILER
    "${RV1106_TOOLCHAIN_BIN}/arm-rockchip830-linux-uclibcgnueabihf-g++")

# Specify the sysroot (if available)
set(CMAKE_SYSROOT
    "${RV1106_SDK_PATH}/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/arm-buildroot-linux-uclibcgnueabihf/sysroot")

if(NOT EXISTS "${CMAKE_C_COMPILER}" OR
   NOT EXISTS "${CMAKE_CXX_COMPILER}" OR
   NOT IS_DIRECTORY "${CMAKE_SYSROOT}")
    message(FATAL_ERROR
        "RV1106 toolchain is incomplete under ${RV1106_SDK_PATH}. "
        "Set -DRV1106_SDK_PATH=/path/to/rv1106-sdk if the SDK is elsewhere.")
endif()

# Add paths to find libraries and includes
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Keep pkg-config from selecting x86 host libraries during cross-compilation.
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
    "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
