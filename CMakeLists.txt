cmake_minimum_required(VERSION 3.15)

project(anc)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

# 查找 ALSA 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)
if(NOT ALSA_FOUND)
    message(FATAL_ERROR "ALSA library not found. Please install libasound2-dev")
endif()

# 查找数学库（不需要 pkg-config，直接使用）
find_library(MATH_LIB m)
if(NOT MATH_LIB)
    message(FATAL_ERROR "Math library not found")
endif()

include_directories(inc)

# 添加可执行文件
add_executable(anc
    src/main.c
)
add_executable(loop
    src/loopback.c
)
add_executable(pb_test
    src/pb_test.c
)

# 链接 loop（只需要 ALSA）
target_link_libraries(loop ${ALSA_LIBRARIES})
target_include_directories(loop PRIVATE ${ALSA_INCLUDE_DIRS})
target_compile_options(loop PRIVATE ${ALSA_CFLAGS_OTHER})

# 链接 pb_test（需要 ALSA + 数学库）
target_link_libraries(pb_test ${ALSA_LIBRARIES} ${MATH_LIB})
target_include_directories(pb_test PRIVATE ${ALSA_INCLUDE_DIRS})
target_compile_options(pb_test PRIVATE ${ALSA_CFLAGS_OTHER})

# 链接 anc（如果需要音频功能，取消下面的注释）
# target_link_libraries(anc ${ALSA_LIBRARIES})
# target_include_directories(anc PRIVATE ${ALSA_INCLUDE_DIRS})
# target_compile_options(anc PRIVATE ${ALSA_CFLAGS_OTHER})

# 显示链接信息
message(STATUS "ALSA libraries: ${ALSA_LIBRARIES}")
message(STATUS "ALSA includes: ${ALSA_INCLUDE_DIRS}")
message(STATUS "Math library: ${MATH_LIB}")