cmake_minimum_required(VERSION 3.16)

# Reported over BLE (INFO characteristic) and in the boot log.
set(PROJECT_VER "0.4.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build components that main (transitively) requires.
idf_build_set_property(MINIMAL_BUILD ON)

project(pukk)
