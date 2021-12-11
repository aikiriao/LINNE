cmake_minimum_required(VERSION 3.15)

# コーデックライブラリ
project(LINNECodecLibrary C)
set(CODEC_LIB_NAME linnecodec)
add_library(${CODEC_LIB_NAME}
    STATIC
    $<TARGET_OBJECTS:linne_encoder>
    $<TARGET_OBJECTS:linne_decoder>
    $<TARGET_OBJECTS:linne_coder>
    $<TARGET_OBJECTS:linne_internal>
    $<TARGET_OBJECTS:linne_network>
    $<TARGET_OBJECTS:bit_stream>
    $<TARGET_OBJECTS:lpc>
    )

# デコーダライブラリ
project(LINNEDecoderLibrary C)
set(DECODER_LIB_NAME linnedec)
add_library(${DECODER_LIB_NAME}
    STATIC
    $<TARGET_OBJECTS:linne_decoder>
    $<TARGET_OBJECTS:linne_coder>
    $<TARGET_OBJECTS:linne_internal>
    $<TARGET_OBJECTS:bit_stream>
    )

# 依存するプロジェクト
add_subdirectory(libs)

# テスト
if(NOT without-test)
    enable_testing()
    # C++環境でないとgtestがビルドできないので、CXXプロジェクトを作る
    # -> Cとの挙動で差異が生じるかもしれない...
    project(LINNETest CXX)
    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    else()
        set(CMAKE_CXX_FLAGS "-std=gnu++11") # gtestがGNU独自拡張を使用しているため
        set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    endif()
    include(cmake/gtest.cmake)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_subdirectory(test)
endif()
