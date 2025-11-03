cmake_minimum_required(VERSION 3.15)
project(dcmloupe VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)  # MSVC for windows
else()
    add_compile_options(-Wall -Wextra -pedantic)  # GCC/Clang for linux/macOS
endif()

set(SOURCES
        src/dicom_dict.c
        src/dicom_header_parser.c
        src/dicom_display.c
        src/main.c
)

set(HEADERS
        lib/dicom_dict.h
        lib/dicom_header_parser.h
        lib/dicom_display.h
)

include_directories(${CMAKE_SOURCE_DIR}/lib)

add_executable(dcmloupe ${SOURCES} ${HEADERS})

if(WIN32)
    target_compile_definitions(dcmloupe PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(UNIX)
    target_link_libraries(dcmloupe m)  # link math
endif()

install(TARGETS dcmloupe DESTINATION bin)

set(CPACK_PACKAGE_NAME "dcmloupe")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)