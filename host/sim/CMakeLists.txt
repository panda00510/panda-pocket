cmake_minimum_required(VERSION 3.16)
project(panda_sim C)
set(CMAKE_C_STANDARD 11)
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../build/lvgl lvgl_build EXCLUDE_FROM_ALL)
find_package(SDL2 REQUIRED)
set(C ${CMAKE_CURRENT_SOURCE_DIR}/../../components)
add_executable(sim main.c backend.c
    ${C}/pp_ui/ui.c ${C}/pp_ui/fonts/pp_font_cjk_14.c ${C}/pp_ui/fonts/pp_font_cjk_16.c ${C}/pp_ui/fonts/pp_font_cjk_20.c ${C}/pp_i18n/i18n.c
    ${C}/pp_bus/bus_parse.c ${C}/pp_bus/bus_mock.c ${C}/pp_nav/nav_parse.c ${C}/pp_nav/nav_mock.c ${C}/pp_storage/favs.c ${C}/pp_wifi/ap_list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/cJSON/cJSON.c)
if(SIM_SCREEN_OFF_S)
    target_compile_definitions(sim PRIVATE CONFIG_PP_SCREEN_OFF_S=${SIM_SCREEN_OFF_S})   # tests: short idle timeout
endif()
target_compile_definitions(sim PRIVATE PP_SIM LV_CONF_PATH="${LV_CONF_PATH}" _GNU_SOURCE)
target_include_directories(sim PRIVATE shim ${C}/hal_board ${C}/pp_ui ${C}/pp_bus ${C}/pp_nav ${C}/pp_storage
    ${C}/pp_wifi ${C}/pp_i18n ${C}/pp_http ${CMAKE_CURRENT_SOURCE_DIR}/../build/cJSON ${SDL2_INCLUDE_DIRS})
target_link_libraries(sim PRIVATE lvgl SDL2::SDL2 m)
# LVGL's SDL driver includes <SDL2/SDL.h>, so the parent of SDL2's include dir must be visible to it.
list(GET SDL2_INCLUDE_DIRS 0 SDL_INC)
get_filename_component(SDL_PARENT ${SDL_INC} DIRECTORY)
target_include_directories(lvgl PUBLIC ${SDL_PARENT} ${SDL_INC})
