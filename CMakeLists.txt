cmake_minimum_required(VERSION 3.27)

project(audio)

if(DEFINED ENV{VCPKG_ROOT})
  message("Bootstrapping vcpkg in $ENV{VCPKG_ROOT} : ${NODE_ARCH}")
  if (${NODE_ARCH} STREQUAL "x64")
    set(VCPKG_TARGET_TRIPLET "x64-windows-static")
  else()
    set(VCPKG_TARGET_TRIPLET "x86-windows-static")
  endif()
  set(VCPKG_LIBRARY_LINKAGE static)
  set(CMAKE_PREFIX_PATH $ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET})
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

set(ENABLE_SAMPLE_PROGRAMS OFF CACHE BOOL "")

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path")
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/..")
endif()

option(BUILD_WEBM_DLL "build webm shared library" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(WIN32)
  add_compile_definitions(UNICODE _UNICODE)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(FFMPEG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_definitions(-DNAPI_VERSION=8)
include_directories(${CMAKE_JS_INC})
include_directories(${FFMPEG_INCLUDE_DIRS})
link_directories(${FFMPEG_LIBRARY_DIRS})
link_libraries(${FFMPEG_LIBRARIES})


# Include N-API wrappers
execute_process(COMMAND node -p "require('node-addon-api').include"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE NODE_ADDON_API_DIR
                )
string(REPLACE "\n" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})

file(GLOB SOURCE_FILES "src/*.cc" "src/*.c" "src/webvtt/*.cc")

set(FFMPEG_SRC
    src/ffmpeg.c
    src/ffmpeg_dec.c
    src/ffmpeg_demux.c
    src/ffmpeg_enc.c
    src/ffmpeg_filter.c
    src/ffmpeg_hw.c
    src/ffmpeg_mux.c
    src/ffmpeg_mux_init.c
    src/ffmpeg_opt.c
    src/objpool.c
    src/sync_queue.c
    src/thread_queue.c
    src/cmdutils.c
    src/opt_common.c
  )
set(FFPROBE_SRC src/ffprobe.c src/cmdutils.c src/opt_common.c)

if(MSVC)
  add_definitions(/wd4706 /wd4819 /wd4701 /wd4703)
endif(MSVC)

add_executable(ffprobe ${FFPROBE_SRC})
target_compile_options(ffprobe PRIVATE /experimental:c11atomics)
target_include_directories(ffprobe PRIVATE src src/compat)

add_executable(ffmpeg ${FFMPEG_SRC})
target_compile_options(ffmpeg PRIVATE /experimental:c11atomics)
target_include_directories(ffmpeg PRIVATE src src/compat)
if(MSVC)
  target_compile_options(ffprobe PRIVATE /wd4100 /wd4389 /wd4244)
  target_compile_options(ffmpeg PRIVATE /wd4100 /wd4389 /wd4244)

add_subdirectory(libwebm)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILD_DLL NAPI_DISABLE_CPP_EXCEPTIONS)
target_include_directories(${PROJECT_NAME} PRIVATE . libwebm src)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB} webm fmt::fmt-header-only avcpp)
if (WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE delayimp.lib)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_compile_options(${PROJECT_NAME} PRIVATE /experimental:c11atomics)

if (BUILD_WEBM_DLL)
  add_library(webmdll SHARED
      "src/webvtt/vttreader.cc" "src/webvtt/webvttparser.cc" "src/webm_muxer.cc" "src/sample_muxer_metadata.cc"
  )
  set_target_properties(webmdll PROPERTIES RUNTIME_OUTPUT_NAME "webm")
  target_link_libraries(webmdll webm)
  target_include_directories(webmdll PRIVATE . libwebm)
endif()

add_subdirectory(src/avcpp)
add_subdirectory(sample)

if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
  # Generate node.lib
  execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF} /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
endif()

# Turn on exporting compile commands json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
IF( EXISTS "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json" )
  EXECUTE_PROCESS( COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json
  )
ENDIF()

if (DEFINED ENV{FFMPEG_ROOT})
  if (WIN32)
    file(GLOB FFMPEG_BIN_FILES "$ENV{FFMPEG_ROOT}/bin/*.exe" "$ENV{FFMPEG_ROOT}/bin/*.dll")
  else()
    file(GLOB FFMPEG_BIN_FILES "$ENV{FFMPEG_ROOT}/lib/*.so")
  endif()
  message(info ">>>COPY: ${FFMPEG_BIN_FILES}")

  if (WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_BIN_FILES} $<TARGET_FILE_DIR:${PROJECT_NAME}>
        DEPENDS $<TARGET_FILE_DIR:ffprobe>/avcodec-60.dll
      )
  else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_BIN_FILES} $<TARGET_FILE_DIR:${PROJECT_NAME}>
        DEPENDS $<TARGET_FILE_DIR:ffprobe>
      )
  endif()
else()
  message(FATAL "FFMPEG_ROOT not set")
endif()
