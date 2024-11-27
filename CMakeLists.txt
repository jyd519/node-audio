cmake_minimum_required(VERSION 3.27)

project(audio)

set(ENABLE_SAMPLE_PROGRAMS OFF CACHE BOOL "")

message(STATUS "CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}")

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "loong64")
  message(STATUS ">>>>Disable webm info and ts support for loongarch64")
  set(ENABLE_WEBMINFO OFF CACHE BOOL "")
  set(ENABLE_WEBMTS OFF CACHE BOOL "")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

if(APPLE)
  set(CMAKE_INSTALL_RPATH "@executable_path")
else()
  set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/..")
endif()

option(BUILD_WEBM_DLL "build webm shared library" ON)
option(ENABLE_FFMPEG "build with ffmpeg library" ON)
option(ENABLE_FFMPEG_FULL "build with ffprobe/ffmpeg" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(WIN32)
  add_compile_definitions(UNICODE _UNICODE)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(ENABLE_FFMPEG)
  find_package(ffmpeg REQUIRED)
endif(ENABLE_FFMPEG)

# find_package(OpenSSL REQUIRED)
# find_package(fmt CONFIG REQUIRED)

add_definitions(-DNAPI_VERSION=8)
include_directories(${CMAKE_JS_INC})

if(ENABLE_FFMPEG)
  include_directories(${FFMPEG_INCLUDE_DIRS})
  link_directories(${FFMPEG_LIBRARY_DIRS})
  link_libraries(${FFMPEG_LIBRARIES})
endif(ENABLE_FFMPEG)

# Include N-API wrappers
execute_process(
  COMMAND node -p "require('node-addon-api').include"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_DIR)
string(REPLACE "\n" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})

file(GLOB SOURCE_FILES "src/webvtt/*.cc")
list(
  APPEND
  SOURCE_FILES
  src/audio-napi.cc
  src/napi_help.cc
  src/webm_muxer.cc
  src/webvtt/vttreader.cc
  src/webvtt/webvttparser.cc
  src/sample_muxer_metadata.cc)

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
    src/opt_common.c)
set(FFPROBE_SRC src/ffprobe.c src/cmdutils.c src/opt_common.c)

if(ENABLE_FFMPEG)
  if (ENABLE_FFMPEG_FULL)
    list(APPEND SOURCE_FILES ${FFPROBE_SRC} ${FFMPEG_SRC})
  endif(ENABLE_FFMPEG_FULL)

  list(
    APPEND
    SOURCE_FILES
    src/av_api.cc
    src/screen_capturer.cc
    src/recorder_api.cc
    src/duration.c
    src/combine.cc
    src/buffer_io.c
    src/get_volume.c)
endif(ENABLE_FFMPEG)

if(MSVC)
  add_definitions(/wd4706 /wd4819 /wd4701 /wd4703)
endif(MSVC)

if (ENABLE_FFMPEG)
  add_subdirectory(src/avcpp)
endif()

if(ENABLE_FFMPEG AND ENABLE_FFMPEG_FULL)
  add_executable(ffprobe ${FFPROBE_SRC})
  target_include_directories(ffprobe PRIVATE src src/compat)

  if (LINUX)
    target_link_libraries(ffprobe PRIVATE m)
  endif()

  add_executable(ffmpeg ${FFMPEG_SRC})
  target_include_directories(ffmpeg PRIVATE src src/compat)
  if(MSVC)
    # target_compile_options(ffmpeg PRIVATE /experimental:c11atomics /std:c11)
    target_compile_options(ffprobe PRIVATE /experimental:c11atomics /std:c11)
    target_compile_options(ffprobe PRIVATE /wd4100 /wd4389 /wd4244)
    # target_compile_options(ffmpeg PRIVATE /wd4100 /wd4389 /wd4244)
  endif(MSVC)

  # add_subdirectory(sample)
endif()

if(BUILD_WEBM_DLL)
  add_subdirectory(libwebm)
endif(BUILD_WEBM_DLL)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILD_DLL
                                                   NAPI_DISABLE_CPP_EXCEPTIONS)
target_include_directories(${PROJECT_NAME} PRIVATE libwebm src)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB} webm)


message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "loong64")
  target_link_options(${PROJECT_NAME} PRIVATE "-L=/usr/lib/loongarch64-linux-gnu")
endif()

if(WIN32)
  target_compile_options(${PROJECT_NAME} PRIVATE /experimental:c11atomics
                                                 /std:c11)
  target_link_libraries(${PROJECT_NAME} PRIVATE delayimp.lib)
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE asound)
endif()

if(ENABLE_FFMPEG)
  if (ENABLE_FFMPEG_FULL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_FFMPEG=2)
  else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_FFMPEG=1)
  endif(ENABLE_FFMPEG_FULL)
  target_include_directories(${PROJECT_NAME} PRIVATE ./src/compat)
  target_link_libraries(${PROJECT_NAME} PRIVATE avcpp)
else()
  target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_FFMPEG=0)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})

if(ENABLE_FFMPEG)
  set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
else()
  set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX
                                                             "-noffmpeg.node")
endif()

if(BUILD_WEBM_DLL)
  set(WEBMDLL "webmdll")
  add_library(
    ${WEBMDLL} SHARED
    "src/webm_muxer.cc" "src/webvtt/vttreader.cc" "src/webvtt/webvttparser.cc"
    "src/sample_muxer_metadata.cc")
  target_include_directories(${WEBMDLL} PRIVATE "./libwebm")
  target_link_libraries(${WEBMDLL} webm)
endif()

if(MSVC
   AND CMAKE_JS_NODELIB_DEF
   AND CMAKE_JS_NODELIB_TARGET)
  # Generate node.lib
  execute_process(
    COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF}
            /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
endif()

# Turn on exporting compile commands json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json")
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
      ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json)
endif()

if(ENABLE_FFMPEG)
  if(WIN32)
    message(info ">>>>${ffmpeg_PACKAGE_FOLDER_RELEASE}")
    file(GLOB FFMPEG_BIN_FILES "$ENV{FFMPEG_ROOT}/bin/*.exe"
         "$ENV{FFMPEG_ROOT}/bin/*.dll")
  else()
    file(GLOB FFMPEG_BIN_FILES "$ENV{FFMPEG_ROOT}/lib/*")
  endif()

  message(info ">>> COPY FFmpeg shared files: ${FFMPEG_BIN_FILES}")

  if(WIN32)
    add_custom_command(
      TARGET ${PROJECT_NAME}
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_BIN_FILES}
        $<TARGET_FILE_DIR:${PROJECT_NAME}> DEPENDS
        $<TARGET_FILE_DIR:audio>/avcodec-60.dll)
  else()
    add_custom_command(
      TARGET ${PROJECT_NAME}
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_BIN_FILES}
        $<TARGET_FILE_DIR:${PROJECT_NAME}> DEPENDS $<TARGET_FILE_DIR:audio>)
  endif()
endif()

get_filename_component(BUILD_NAME "${CMAKE_CURRENT_BINARY_DIR}" NAME)
set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin/${BUILD_NAME}")

if(WIN32)
  set(WEBMNAME "webm.dll")
elseif (APPLE)
  set(WEBMNAME "webm.dylib")
else()
  set(WEBMNAME "libwebm.so")
endif()

add_custom_command(
  TARGET audio
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BIN_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:audio> ${BIN_DIR}/
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${WEBMDLL}>
          ${BIN_DIR}/${WEBMNAME})
