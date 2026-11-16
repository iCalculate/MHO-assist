cmake_minimum_required(VERSION 3.19 FATAL_ERROR)
project(all)

add_subdirectory(QtScrcpy)
find_package(Qt5 REQUIRED COMPONENTS Widgets Network Test)
add_executable(input_reset_test input_reset_test.cpp)
target_include_directories(input_reset_test PRIVATE
 QtScrcpy/QtScrcpyCore/src/device/controller
 QtScrcpy/QtScrcpyCore/src/device/controller/inputconvert
 QtScrcpy/QtScrcpyCore/src/device/controller/inputconvert/keymap
 QtScrcpy/QtScrcpyCore/src/device/android
 QtScrcpy/QtScrcpyCore/src/common)
target_link_libraries(input_reset_test PRIVATE QtScrcpyCore Qt5::Widgets Qt5::Network Qt5::Test)


target_compile_options(input_reset_test PRIVATE /utf-8)
add_executable(panel_layout_test panel_layout_test.cpp QtScrcpy/uibase/keepratiowidget.cpp QtScrcpy/uibase/keepratiowidget.h)
set_target_properties(panel_layout_test PROPERTIES AUTOMOC ON)
target_link_libraries(panel_layout_test PRIVATE Qt5::Widgets)
target_compile_options(panel_layout_test PRIVATE /utf-8)


add_executable(integrated_assist_test integrated_assist_test.cpp)
target_link_libraries(integrated_assist_test PRIVATE Qt5::Widgets Qt5::Test)
target_compile_options(integrated_assist_test PRIVATE /utf-8)
