add_library(dhw_render_qt STATIC
    src/qt_renderer.cpp
    src/qt_text.cpp
)

add_library(DaHoWork::render_qt ALIAS dhw_render_qt)

target_include_directories(dhw_render_qt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(dhw_render_qt PUBLIC dhw_core Qt6::Gui Qt6::Widgets)
dhw_set_warnings(dhw_render_qt)
