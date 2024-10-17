add_library(common STATIC video_debugging.c)

set(TARGETS transcoding webm2mp4)

foreach(target ${TARGETS})
  if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${target}.cc)
    add_executable(${target} ${target}.cc)
  else ()
    add_executable(${target} ${target}.c)
    target_link_libraries(${target} PRIVATE common)
  endif()

  target_link_libraries(${target} PRIVATE avcpp::avcpp)
endforeach()
