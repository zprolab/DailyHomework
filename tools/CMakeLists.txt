add_executable(dhw_render_cli render_cli/main.cpp)
target_link_libraries(dhw_render_cli PRIVATE dhw_render_qt)
dhw_set_warnings(dhw_render_cli)

# Generates the Windows application icon (assets/app.ico) from the shared
# vector icon set. Only useful next to a Qt build, hence this directory.
add_executable(dhw_make_icon make_icon/main.cpp)
target_link_libraries(dhw_make_icon PRIVATE dhw_render_qt)
dhw_set_warnings(dhw_make_icon)
