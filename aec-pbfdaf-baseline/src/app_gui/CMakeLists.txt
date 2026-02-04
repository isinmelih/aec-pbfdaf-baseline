find_package(Qt6 COMPONENTS Widgets Charts QUIET)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (Qt6_FOUND)
    add_executable(gui_app
        main_qt.cpp
        MainWindow.cpp
        MainWindow.h
        WasapiRunner.cpp
        WasapiRunner.h
        DeviceUtil.cpp
        DeviceUtil.h
    )

    target_link_libraries(gui_app PRIVATE aec_core Qt6::Widgets Qt6::Charts mmdevapi uuid ole32 user32 avrt)
    target_compile_definitions(gui_app PRIVATE NOMINMAX HAVE_QT)
else()
    message(STATUS "Qt6 not found. Skipping GUI app.")
endif()

# Console App (WASAPI AEC)
add_executable(wasapi_aec
    WasapiMain.cpp
    DeviceUtil.cpp
    DeviceUtil.h
)
target_link_libraries(wasapi_aec PRIVATE aec_core mmdevapi uuid ole32 user32 avrt)
target_compile_definitions(wasapi_aec PRIVATE NOMINMAX)

# Win32 Legacy Launcher (Control Panel)
add_executable(control_panel WIN32
    ControlPanel.cpp
    DeviceUtil.cpp
    DeviceUtil.h
)
target_link_libraries(control_panel PRIVATE user32 shell32 comctl32 mmdevapi uuid ole32 avrt)
target_compile_definitions(control_panel PRIVATE NOMINMAX)

# Offline File Processor
add_executable(echocancel
    main.cpp
)
target_link_libraries(echocancel PRIVATE aec_core)
