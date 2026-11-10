cmake_minimum_required(VERSION 3.10)
project(corpus_sentence_aligner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(csa STATIC
  src/utf8.cpp
  src/zip.cpp
  src/document.cpp
  src/segmenter.cpp
  src/gale_church.cpp
  src/aligner.cpp
  src/export.cpp
  src/evaluate.cpp)
target_include_directories(csa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(csa-align src/main.cpp)
target_link_libraries(csa-align PRIVATE csa)

option(CSA_BUILD_TESTS "Build the unit tests" ON)
if(CSA_BUILD_TESTS)
  enable_testing()
  add_executable(csa_tests
    tests/test_main.cpp
    tests/test_utf8.cpp
    tests/test_segmenter.cpp
    tests/test_gale_church.cpp
    tests/test_zip_docx.cpp
    tests/test_export.cpp
    tests/test_integration.cpp)
  target_link_libraries(csa_tests PRIVATE csa)
  target_compile_definitions(csa_tests PRIVATE CSA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  add_test(NAME unit_tests COMMAND csa_tests)
  add_test(NAME cli_smoke
    COMMAND csa-align --quiet --date 20220101T000000Z
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sample_en.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sample_fr.txt
            --gold ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sample_gold.tsv)
endif()

install(TARGETS csa-align RUNTIME DESTINATION bin)
