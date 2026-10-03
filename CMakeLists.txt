cmake_minimum_required(VERSION 4.3)
project(templide)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(miniz
        URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
        URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(miniz)
add_library(miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(miniz PUBLIC ${miniz_SOURCE_DIR})

# 배경 그림을 읽는 데 쓰는 header 하나짜리 라이브러리
FetchContent_Declare(stb
        URL https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h
        URL_HASH SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
        DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(stb)

# --serve가 편집기의 요청(JSON)을 읽는 데 쓰는 header 하나짜리 라이브러리
FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
        URL_HASH SHA256=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6
        DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(templide main.cpp
        frontend/lexor.h
        frontend/lexor.cpp
        frontend/parser.h
        middleend/ir.h
        middleend/analyzer.h
        middleend/analyzer.cpp
        middleend/library.h
        middleend/library.cpp
        middleend/geometry.h
        middleend/geometry.cpp
        backend/backend.h
        backend/backend.cpp
        backend/pptx.h
        backend/pptx.cpp
        backend/html.h
        backend/html.cpp
        backend/pptx_animations.h
        backend/pptx_animations.cpp
        backend/raster.h
        backend/raster.cpp
        server/edits.h
        server/edits.cpp
        server/completion.h
        server/completion.cpp
        server/navigation.h
        server/navigation.cpp
        server/server.h
        server/server.cpp)
target_include_directories(templide PRIVATE ${stb_SOURCE_DIR} ${nlohmann_json_SOURCE_DIR})
target_link_libraries(templide PRIVATE miniz)

# MinGW 런타임(libstdc++ 등)을 exe에 넣어 PATH에 있는 다른 버전의 DLL을 불러오지 않게 한다
if (MINGW)
    target_link_options(templide PRIVATE -static-libgcc -static-libstdc++)
    # CLion의 MinGW는 정적 winpthread가 ucrt용이라 msvcrt 프로그램에 넣을 수 없다.
    # 대신 같은 toolchain의 DLL을 exe 옆에 둔다. exe 폴더의 DLL이 PATH보다 먼저 쓰인다
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
    if (EXISTS ${MINGW_BIN_DIR}/libwinpthread-1.dll)
        add_custom_command(TARGET templide POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MINGW_BIN_DIR}/libwinpthread-1.dll $<TARGET_FILE_DIR:templide>)
    endif ()
endif ()

# installer가 생기기 전까지는 빌드할 때마다 packages(#include <...>로 쓰는 .tlide)와
# libs(html 출력에 넣는 JS)를 exe 옆으로 복사한다
add_custom_target(templide_libs ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/packages $<TARGET_FILE_DIR:templide>/packages
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/libs $<TARGET_FILE_DIR:templide>/libs)
add_dependencies(templide templide_libs)
