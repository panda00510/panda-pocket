cmake_minimum_required(VERSION 3.16)
# Local, git-ignored overrides (API keys): see README.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.secrets")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.secrets")
else()
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(panda-pocket)
