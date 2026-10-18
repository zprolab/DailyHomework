file(GLOB_RECURSE DHW_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(dhw_core STATIC ${DHW_CORE_SOURCES})
add_library(DaHoWork::core ALIAS dhw_core)

target_include_directories(dhw_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(dhw_core PUBLIC cxx_std_20)
dhw_set_warnings(dhw_core)

if(APPLE)
    target_compile_definitions(dhw_core PUBLIC DHW_PLATFORM_MACOS=1)
elseif(WIN32)
    target_compile_definitions(dhw_core PUBLIC DHW_PLATFORM_WINDOWS=1)
elseif(UNIX)
    target_compile_definitions(dhw_core PUBLIC DHW_PLATFORM_LINUX=1)
endif()
