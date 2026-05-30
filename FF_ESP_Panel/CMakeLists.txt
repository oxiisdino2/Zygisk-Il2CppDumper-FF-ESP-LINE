cmake_minimum_required(VERSION 3.10)
project(FF_ESP_Panel)

set(CMAKE_CXX_STANDARD 17)

# Windows GUI application
add_executable(FF_ESP_Panel WIN32
    src/main.cpp
    src/overlay.cpp
    src/memory.cpp
    src/w2s.cpp
    src/esp.cpp
)

target_include_directories(FF_ESP_Panel PRIVATE src)

target_link_libraries(FF_ESP_Panel PRIVATE
    gdi32
    user32
    d2d1
    dwrite
    ws2_32
)
