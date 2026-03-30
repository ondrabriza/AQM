# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources})

#idf_component_register(SRCS "main.c"
#                       INCLUDE_DIRS "."
#                       REQUIRES log nvs_flash freertos aqm_config aqm_i2c aqm_datastore ads1115 sen55 aqm_wifi aqm_modbus aqm_gpio aqm_tasks)