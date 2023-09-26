cmake_minimum_required(VERSION 3.16)

project(
    ocean
    VERSION 0.1
    LANGUAGES C
)

add_executable(ocean src/main.c)
set_property(TARGET ocean PROPERTY C_STANDARD 90)
if(MSVC)
  target_compile_options(ocean PRIVATE /W4 /WX)
else()
  target_compile_options(ocean PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

target_include_directories(ocean PUBLIC include)

find_package(Curses REQUIRED)
target_include_directories(ocean PUBLIC ${CURSES_INCLUDE_DIR})
target_link_libraries(ocean PUBLIC ${CURSES_LIBRARY})
