cmake_minimum_required(VERSION 3.15)

# 明确指定项目为 C 语言，防止 C++ 标志注入
project(anc C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

# --- 跨平台库查找策略 ---

# 1. 查找数学库 (Windows 通常内置于运行时，Unix 需要 -lm)
if(NOT WIN32)
    find_library(MATH_LIB m)
endif()

# 2. 查找 ALSA (仅限 Linux)
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(ALSA alsa)
    endif()

    # 如果 pkg-config 没找到，尝试直接查找
    if(NOT ALSA_FOUND)
        find_path(ALSA_INCLUDE_DIRS NAMES alsa/asoundlib.h)
        find_library(ALSA_LIBRARIES NAMES asound)
        if(ALSA_INCLUDE_DIRS AND ALSA_LIBRARIES)
            set(ALSA_FOUND TRUE)
        endif()
    endif()
endif()

# --- 可执行文件定义 ---

# 通用包含目录
set(INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/inc)

# Windows 特定构建
if(WIN32)
    add_executable(fir src/fir_demo.c)
    target_include_directories(fir PRIVATE ${INC_DIR})
    if(MATH_LIB)
        target_link_libraries(fir PRIVATE ${MATH_LIB})
    endif()
    message(STATUS "Windows build: ALSA features disabled.")
else()
    # Linux/Unix 构建
    add_executable(anc src/main.c)
    target_include_directories(anc PRIVATE ${INC_DIR})

    add_executable(loop src/loopback.c)
    target_include_directories(loop PRIVATE ${INC_DIR})

    add_executable(pb_test src/pb_test.c)
    target_include_directories(pb_test PRIVATE ${INC_DIR})

    add_executable(lms src/lms_anc_demo.c)
    target_include_directories(lms PRIVATE ${MATH_LIB})

    # 链接 ALSA (如果找到)
    if(ALSA_FOUND)
        foreach(target anc loop pb_test)
            target_link_libraries(${target} PRIVATE ${ALSA_LIBRARIES})
            target_include_directories(${target} PRIVATE ${ALSA_INCLUDE_DIRS})
            if(ALSA_CFLAGS_OTHER)
                target_compile_options(${target} PRIVATE ${ALSA_CFLAGS_OTHER})
            endif()
        endforeach()
        message(STATUS "ALSA found: ${ALSA_LIBRARIES}")
    endif()

    # 链接数学库
    if(MATH_LIB)
        target_link_libraries(pb_test PRIVATE ${MATH_LIB})
        target_link_libraries(lms PRIVATE ${MATH_LIB})
    endif()
endif()

message(STATUS "Math library: ${MATH_LIB}")