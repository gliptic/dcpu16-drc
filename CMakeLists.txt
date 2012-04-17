cmake_minimum_required (VERSION 2.6)

project (Dcpu16drc)

include_directories("${PROJECT_SOURCE_DIR}/..")

add_executable(Dcpu16drc vm.c asm.c parser.c tl.c)
target_link_libraries(Dcpu16drc udis86)
