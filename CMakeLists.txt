cmake_minimum_required(VERSION 3.18)
project(
  lex
  VERSION 0.1.0
  LANGUAGES C CXX)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.[ch]" "src/*.[ch]pp")
add_executable(${PROJECT_NAME} ${SOURCES})

add_subdirectory(cbitset)
target_link_libraries(${PROJECT_NAME} PRIVATE cbitset gc)
project(vec)
add_library(vec "vec/src/vec.c")
target_include_directories(vec PUBLIC "vec/src")
target_link_libraries(lex PRIVATE vec)
target_include_directories(lex PRIVATE "uthash/include")
