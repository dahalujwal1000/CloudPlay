enable_language(C)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOTION_EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(MOTION_GLES REQUIRED IMPORTED_TARGET glesv2)
find_path(WAYLAND_INCLUDE_DIR wayland-client.h REQUIRED)
find_library(WAYLAND_CLIENT_LIBRARY NAMES wayland-client libwayland-client.so.0 REQUIRED)
find_library(WAYLAND_EGL_LIBRARY NAMES wayland-egl libwayland-egl.so.1 REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
find_path(WAYLAND_PROTOCOLS_DIR stable/xdg-shell/xdg-shell.xml
  PATHS /usr/share/wayland-protocols /usr/local/share/wayland-protocols
  PATH_SUFFIXES share/wayland-protocols REQUIRED)
set(motion_generated "${CMAKE_CURRENT_BINARY_DIR}/motion-protocols")
file(MAKE_DIRECTORY "${motion_generated}")
set(motion_protocol_sources)
foreach(protocol xdg-shell presentation-time)
  set(xml "${WAYLAND_PROTOCOLS_DIR}/stable/${protocol}/${protocol}.xml")
  set(header "${motion_generated}/${protocol}-client-protocol.h")
  set(source "${motion_generated}/${protocol}-protocol.c")
  add_custom_command(OUTPUT "${header}" "${source}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${header}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${source}"
    DEPENDS "${xml}" VERBATIM)
  list(APPEND motion_protocol_sources "${header}" "${source}")
endforeach()
add_executable(cloudplay_wayland_motion linux/motion_source.cpp ${motion_protocol_sources})
target_include_directories(cloudplay_wayland_motion PRIVATE "${motion_generated}" "${WAYLAND_INCLUDE_DIR}")
target_link_libraries(cloudplay_wayland_motion PRIVATE cloudplay_options cloudplay_capture_policy
  PkgConfig::MOTION_EGL PkgConfig::MOTION_GLES "${WAYLAND_CLIENT_LIBRARY}" "${WAYLAND_EGL_LIBRARY}")
if(BUILD_TESTING)
  add_test(NAME capture.motion_cli COMMAND "${CMAKE_COMMAND}"
    -DMOTION=$<TARGET_FILE:cloudplay_wayland_motion> -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion_cli_tests.cmake")
endif()
