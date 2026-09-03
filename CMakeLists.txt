cmake_minimum_required(VERSION 3.16)

project(VMS_v2 VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets OpenGLWidgets Network WebSockets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets OpenGLWidgets Network WebSockets Concurrent)
find_package(PkgConfig QUIET)

set(VMS_WITH_GSTREAMER 0)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET gstreamer-1.0 gstreamer-video-1.0 gstreamer-app-1.0)
    if(GSTREAMER_FOUND)
        set(VMS_WITH_GSTREAMER 1)
    endif()
endif()

file(GLOB_RECURSE PROJECT_CPP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE PROJECT_HEADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)
set(PROJECT_SOURCES
    ${PROJECT_CPP_SOURCES}
    ${PROJECT_HEADER_SOURCES}
    resources.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(VMS_v2
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET VMS_v2 APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(VMS_v2 SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
add_executable(VMS_v2
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(VMS_v2 PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::OpenGLWidgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets Qt${QT_VERSION_MAJOR}::Concurrent)
target_compile_definitions(VMS_v2 PRIVATE VMS_WITH_GSTREAMER=${VMS_WITH_GSTREAMER})
target_include_directories(VMS_v2 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/screens"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/services"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/media"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/screens"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/services"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/media"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
)
if(VMS_WITH_GSTREAMER)
    target_include_directories(VMS_v2 PRIVATE ${GSTREAMER_INCLUDE_DIRS})
    target_link_directories(VMS_v2 PRIVATE ${GSTREAMER_LIBRARY_DIRS})
    target_link_libraries(VMS_v2 PRIVATE ${GSTREAMER_LIBRARIES})
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app_config.json")
    add_custom_command(TARGET VMS_v2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/app_config.json"
            "$<TARGET_FILE_DIR:VMS_v2>/app_config.json")
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.VMS_v2)
endif()
set_target_properties(VMS_v2 PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE FALSE
)

include(GNUInstallDirs)
install(TARGETS VMS_v2
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app_config.json")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/app_config.json"
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(VMS_v2)
endif()
