add_executable(dhw_render_cli render_cli/main.cpp)
target_link_libraries(dhw_render_cli PRIVATE dhw_render_qt)
dhw_set_warnings(dhw_render_cli)

# Generates the Windows application icon (assets/app.ico) from the shared
# vector icon set. Only useful next to a Qt build, hence this directory.
add_executable(dhw_make_icon make_icon/main.cpp)
target_link_libraries(dhw_make_icon PRIVATE dhw_render_qt)
dhw_set_warnings(dhw_make_icon)

if(DHW_STATIC_QT)
    # Both tools construct a QGuiApplication; with a static Qt the platform and
    # image-format plugins need to be linked in explicitly.
    qt_import_plugins(dhw_render_cli INCLUDE_BY_TYPE platforms imageformats)
    qt_import_plugins(dhw_make_icon INCLUDE_BY_TYPE platforms imageformats)
endif()
