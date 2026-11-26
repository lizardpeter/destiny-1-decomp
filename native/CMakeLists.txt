cmake_minimum_required(VERSION 3.16)
project(d1_native C CXX)
if(WIN32)
  enable_language(RC)  # application manifest (UTF-8 code page)
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  message(FATAL_ERROR "d1-native runs the game's x86-64 code directly; it needs an x86-64 host")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

# NID -> name table for imports we have not implemented (log readability only).
set(IMPORT_SURFACE ${CMAKE_CURRENT_SOURCE_DIR}/../records/import_names.tsv)
set(IMPORT_NAMES ${CMAKE_CURRENT_BINARY_DIR}/import_names.inc)
add_custom_command(
  OUTPUT ${IMPORT_NAMES}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_import_names.py ${IMPORT_SURFACE} ${IMPORT_NAMES}
  DEPENDS ${IMPORT_SURFACE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_import_names.py)

# Native renderer library: our own GCN decoder, SPIR-V builder, shader translator and Vulkan
# layer. Vulkan headers: Khronos Vulkan-Headers (header-only, fetched at configure time); the
# loader is opened at run time.
include(FetchContent)
FetchContent_Declare(vulkan_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG v1.4.365
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(vulkan_headers)
if(NOT vulkan_headers_POPULATED)
  FetchContent_Populate(vulkan_headers)
endif()
add_library(d1_gfx STATIC
  gfx/gcn_decode.cpp
  gfx/spirv.cpp
  gfx/translate.cpp
  gfx/tiling.cpp
  gfx/formats.cpp
  gfx/nrender.cpp
  gfx/vk/vk_context.cpp)
target_include_directories(d1_gfx PUBLIC gfx ${vulkan_headers_SOURCE_DIR}/include)
if(NOT WIN32)
  target_link_libraries(d1_gfx PUBLIC ${CMAKE_DL_LIBS})
endif()

add_executable(d1-native
  src/main.cpp
  src/loader.cpp
  src/runtime.cpp
  src/pc_libc_imports.cpp
  src/fios_imports.cpp
  src/hle_kernel.cpp
  src/hle_pthread.cpp
  src/hle_services.cpp
  src/hle_input.cpp
  src/hle_gpu.cpp
  src/hle_audio.cpp
  src/hle_net.cpp
  src/hle_http.cpp
  src/hle_npwebapi.cpp
  src/hooks.cpp
  src/native_gfx.cpp
  src/settings.cpp
  src/discord.cpp
  ${IMPORT_NAMES})
if(WIN32)
  target_sources(d1-native PRIVATE src/os_win.cpp win/d1-native.rc)
  # One self-contained .exe: C/C++ runtime and winpthreads linked in statically.
  target_link_libraries(d1-native PRIVATE ws2_32 winmm -static)
  target_compile_definitions(d1-native PRIVATE _FILE_OFFSET_BITS=64 _WIN32_WINNT=0x0A00 WINVER=0x0A00)
else()
  target_sources(d1-native PRIVATE src/os_posix.cpp)
endif()
target_include_directories(d1-native PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(d1-native PRIVATE Threads::Threads d1_gfx)
target_compile_options(d1-native PRIVATE -Wall -Wno-unused-function -fno-omit-frame-pointer)
if(NOT WIN32)
  # Logs print 64-bit values with %llx (correct on both systems; uint64_t is unsigned long here).
  target_compile_options(d1-native PRIVATE -Wno-format)
endif()

# Window, input and audio (SDL2; on Windows linked in statically, so no SDL2.dll next to the exe).
option(D1_NATIVE_WINDOW "Game window, input and audio with SDL2" ON)
if(D1_NATIVE_WINDOW)
  find_package(SDL2 REQUIRED)
  target_sources(d1-native PRIVATE src/frontend_sdl.cpp)
  target_compile_definitions(d1-native PRIVATE D1_FRONTEND_SDL=1)
  if(WIN32)
    target_link_libraries(d1-native PRIVATE SDL2::SDL2-static)
  else()
    target_link_libraries(d1-native PRIVATE SDL2::SDL2)
  endif()
else()
  target_sources(d1-native PRIVATE src/frontend_headless.cpp)
endif()

# Development tool for the native shader translator (decode / disassemble / translate).
if(NOT WIN32)
  add_executable(d1-shadertool gfx/tools/d1_shadertool.cpp)
  target_link_libraries(d1-shadertool PRIVATE d1_gfx)
  add_executable(tiling-fast-test gfx/tests/tiling_fast_test.cpp)
  target_link_libraries(tiling-fast-test PRIVATE d1_gfx)
endif()

# Standalone native memory metadata regression (no Vulkan or game assets required).
add_executable(region-extent-cache-test tests/region_extent_cache_test.cpp)
target_include_directories(region-extent-cache-test PRIVATE src)
target_link_libraries(region-extent-cache-test PRIVATE Threads::Threads)
enable_testing()
add_test(NAME region-extent-cache COMMAND region-extent-cache-test)

add_executable(native-record-registry-test tests/native_record_registry_test.cpp)
target_include_directories(native-record-registry-test PRIVATE src)
target_link_libraries(native-record-registry-test PRIVATE Threads::Threads)
add_test(NAME native-record-registry COMMAND native-record-registry-test)

add_executable(frame-time-contract-test tests/frame_time_contract_test.cpp)
target_include_directories(frame-time-contract-test PRIVATE src)
add_test(NAME frame-time-contract COMMAND frame-time-contract-test)

add_executable(native-ranges-test tests/native_ranges_test.cpp)
target_include_directories(native-ranges-test PRIVATE src)
add_test(NAME native-ranges COMMAND native-ranges-test)

# Tests actual emitted SPIR-V with a narrow CPU evaluator, without a GPU.
add_executable(query-reduce-test tests/query_reduce_test.cpp gfx/spirv.cpp)
target_include_directories(query-reduce-test PRIVATE gfx)
add_test(NAME query-reduce COMMAND query-reduce-test)

add_executable(pc-libc-leaf-test tests/pc_libc_leaf_test.cpp)
target_include_directories(pc-libc-leaf-test PRIVATE src)
add_test(NAME pc-libc-leaf COMMAND pc-libc-leaf-test)

add_executable(pc-file-test tests/pc_file_test.cpp)
target_include_directories(pc-file-test PRIVATE src)
target_link_libraries(pc-file-test PRIVATE Threads::Threads)
add_test(NAME pc-file COMMAND pc-file-test "${CMAKE_CURRENT_BINARY_DIR}/pc-file-test.bin")

add_executable(pc-paths-test tests/pc_paths_test.cpp)
target_include_directories(pc-paths-test PRIVATE src)
add_test(NAME pc-paths COMMAND pc-paths-test "${CMAKE_CURRENT_BINARY_DIR}/pc-paths-test-dir")

add_executable(pc-mount-test tests/pc_mount_test.cpp)
target_include_directories(pc-mount-test PRIVATE src)
add_test(NAME pc-mount COMMAND pc-mount-test)

add_executable(pc-service-test tests/pc_service_test.cpp)
target_include_directories(pc-service-test PRIVATE src)
target_link_libraries(pc-service-test PRIVATE Threads::Threads)
add_test(NAME pc-service COMMAND pc-service-test "${CMAKE_CURRENT_BINARY_DIR}/pc-service-test-dir")
add_executable(fios-contract-test tests/fios_contract_test.cpp)
target_include_directories(fios-contract-test PRIVATE src)
add_test(NAME fios-contract COMMAND fios-contract-test)

add_executable(pc-libc-math-test tests/pc_libc_math_test.cpp)
target_include_directories(pc-libc-math-test PRIVATE src)
target_compile_options(pc-libc-math-test PRIVATE -frounding-math)
add_test(NAME pc-libc-math COMMAND pc-libc-math-test)

add_executable(pc-staging-test tests/pc_staging_test.cpp)
target_include_directories(pc-staging-test PRIVATE src)
target_link_libraries(pc-staging-test PRIVATE Threads::Threads)
add_test(NAME pc-staging COMMAND pc-staging-test "${CMAKE_CURRENT_BINARY_DIR}/pc-staging-test.bin")

add_executable(pc-callback-test tests/pc_callback_test.cpp)
target_include_directories(pc-callback-test PRIVATE src)
target_link_libraries(pc-callback-test PRIVATE Threads::Threads)
add_test(NAME pc-callback COMMAND pc-callback-test "${CMAKE_CURRENT_BINARY_DIR}/pc-callback-test.bin")
set_tests_properties(pc-callback pc-staging PROPERTIES TIMEOUT 30)

add_executable(fios-binary-contract-test tests/fios_binary_contract_test.cpp)
target_include_directories(fios-binary-contract-test PRIVATE src)
target_link_libraries(fios-binary-contract-test PRIVATE Threads::Threads)
add_test(NAME fios-binary-contract COMMAND fios-binary-contract-test)
set_tests_properties(fios-binary-contract PROPERTIES TIMEOUT 30)

add_executable(fios-native-core-test tests/fios_native_core_test.cpp)
target_include_directories(fios-native-core-test PRIVATE src)
target_link_libraries(fios-native-core-test PRIVATE Threads::Threads)
add_test(NAME fios-native-core COMMAND fios-native-core-test "${CMAKE_CURRENT_BINARY_DIR}/fios-native-core-test-dir")
set_tests_properties(fios-native-core PROPERTIES TIMEOUT 30)

# Uses the real production adapter and callback assembly, with simulated retail
# runtime/TLS/mapping hooks. Does not load game modules or require a GPU.
if(NOT WIN32)
  add_executable(fios-imports-test tests/fios_imports_test.cpp src/fios_imports.cpp)
  target_include_directories(fios-imports-test PRIVATE src)
  target_link_libraries(fios-imports-test PRIVATE Threads::Threads)
  add_test(NAME fios-imports COMMAND fios-imports-test "${CMAKE_CURRENT_BINARY_DIR}/fios-imports-test-dir")
  set_tests_properties(fios-imports PROPERTIES TIMEOUT 30)
endif()
