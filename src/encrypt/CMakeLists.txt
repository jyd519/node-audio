project(encrypt LANGUAGES C)

if(WIN32)
  add_compile_definitions(UNICODE _UNICODE)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(OpenSSL REQUIRED)

add_executable(test_writer joye_writer.c joye_writer.h test_writer.c)
add_executable(test_reader joye_reader.c joye_reader.h test_reader.c)

if(WIN32)
  target_compile_options(test_writer PRIVATE /std:c11)
  target_link_libraries(test_writer PRIVATE delayimp.lib)
endif()

target_link_libraries(test_writer PRIVATE OpenSSL::Crypto)
target_link_libraries(test_reader PRIVATE OpenSSL::Crypto)
