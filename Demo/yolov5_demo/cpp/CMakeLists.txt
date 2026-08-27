cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake" CACHE FILEPATH "Default toolchain file for ARM builds")
    message(STATUS "No toolchain file specified; using default: ${CMAKE_TOOLCHAIN_FILE}")
endif()
message(STATUS "Building for ARM using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")

project(rknn_yolov5_demo)

option(ENABLE_RK_MPI_PIPELINE "Enable the RV1106 VI/VPSS camera backend" ON)

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()

add_definitions(-DRV1106_1103)

set(LIBRKNNRT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/Linux/armhf-uclibc/librknnmrt.so")

message(STATUS "LIB_RKNNRT = ${LIBRKNNRT}")
set(rknpu2_yolov5_file rknpu2/yolov5_rv1106_1103.cc)



add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utils/ utils.out)

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

#Opencv 4 
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS} )

# Add the source files
add_library(yoloCameraCore STATIC
    postprocess.cc
    AIcamera_c_interface.cc
    ${rknpu2_yolov5_file}
)
target_compile_features(yoloCameraCore PUBLIC cxx_std_17)

if(ENABLE_RK_MPI_PIPELINE)
    if(NOT DEFINED RV1106_SDK_PATH)
        get_filename_component(ECHO_MATE_ROOT
            "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
        set(RV1106_SDK_PATH "${ECHO_MATE_ROOT}/SDK/rv1106-sdk"
            CACHE PATH "Path to the RV1106 SDK")
    endif()

    set(RK_MPI_INCLUDE_DIR
        "${RV1106_SDK_PATH}/media/rockit/rockit/mpi/sdk/include")
    set(RK_MPI_LIBRARY
        "${RV1106_SDK_PATH}/media/rockit/out/lib/librockit.so")
    set(RK_MPP_LIBRARY
        "${RV1106_SDK_PATH}/media/mpp/out/lib/librockchip_mpp.so.0")
    set(RK_RGA_LIBRARY
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/librga/Linux/armhf_uclibc/librga.so")
    set(RKAIQ_INCLUDE_ROOT
        "${RV1106_SDK_PATH}/media/isp/out/include/rkaiq")
    set(RKAIQ_LIBRARY
        "${RV1106_SDK_PATH}/media/isp/out/lib/librkaiq.so")

    foreach(REQUIRED_PATH
            "${RK_MPI_INCLUDE_DIR}"
            "${RK_MPI_LIBRARY}"
            "${RK_MPP_LIBRARY}"
            "${RK_RGA_LIBRARY}"
            "${RKAIQ_INCLUDE_ROOT}"
            "${RKAIQ_LIBRARY}")
        if(NOT EXISTS "${REQUIRED_PATH}")
            message(FATAL_ERROR
                "RV1106 media dependency is missing: ${REQUIRED_PATH}")
        endif()
    endforeach()

    target_sources(yoloCameraCore PRIVATE rk_media_pipeline.cc)
    target_compile_definitions(yoloCameraCore PRIVATE
        DESKBOT_HAS_RK_MPI=1
        DESKBOT_HAS_RKAIQ=1)
    target_include_directories(yoloCameraCore SYSTEM PRIVATE
        "${RK_MPI_INCLUDE_DIR}"
        "${RKAIQ_INCLUDE_ROOT}"
        "${RKAIQ_INCLUDE_ROOT}/common"
        "${RKAIQ_INCLUDE_ROOT}/uAPI2"
        "${RKAIQ_INCLUDE_ROOT}/algos"
        "${RKAIQ_INCLUDE_ROOT}/iq_parser"
        "${RKAIQ_INCLUDE_ROOT}/iq_parser_v2"
        "${RKAIQ_INCLUDE_ROOT}/xcore"
        "${RKAIQ_INCLUDE_ROOT}/xcore/base")
    target_link_libraries(yoloCameraCore
        "${RK_MPI_LIBRARY}"
        "${RK_MPP_LIBRARY}"
        "${RK_RGA_LIBRARY}"
        "${RKAIQ_LIBRARY}"
        dl)
endif()

# Link libraries for opus and websocketpp
target_link_libraries(yoloCameraCore
    imageutils
    fileutils
    imagedrawing    
    ${LIBRKNNRT}
    ${OpenCV_LIBS}
)

target_include_directories(yoloCameraCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma
    ${OpenCV_LIBS}
)

set_source_files_properties(
    AIcamera_c_interface.cc
    rk_media_pipeline.cc
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")

# add_executable(c_face_test main2.cc)
# target_link_libraries(c_face_test yoloCameraCore)
# target_include_directories(c_face_test PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/include
#     ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma
#     ${OpenCV_LIBS}
# )


add_executable(${PROJECT_NAME}
    main.cc
)

target_link_libraries(${PROJECT_NAME} yoloCameraCore)


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma
    ${OpenCV_LIBS}
)

set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/rv1106_yolov5_demo")

# 指定运行时动态库的加载路径
# 设置运行时 rpath，以便可执行文件能够在运行时找到指定目录下的共享库
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,\$ORIGIN/lib")

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/bus.jpg DESTINATION ${CMAKE_INSTALL_PREFIX}/model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/coco_80_labels_list.txt DESTINATION ${CMAKE_INSTALL_PREFIX}/model)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION ${CMAKE_INSTALL_PREFIX}/model)
