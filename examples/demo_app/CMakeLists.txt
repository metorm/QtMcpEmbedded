add_executable(demo_app main.cpp)

target_link_libraries(demo_app PRIVATE QtMcpEmbedded Qt${QT_VERSION_MAJOR}::Widgets)

# Deploy Qt DLLs/plugins next to the exe after each build, so the exe can be
# launched directly (double-click / plain shell) without setting PATH.
if(WIN32)
    get_target_property(_qmake_executable Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        message(STATUS "Using windeployqt @ ${WINDEPLOYQT_EXECUTABLE}")
        # $<TARGET_FILE:demo_app> resolves per-configuration, so Debug builds
        # deploy debug DLLs and Release builds deploy release DLLs.
        add_custom_command(TARGET demo_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E
                env PATH="${_qt_bin_dir}"
                "${WINDEPLOYQT_EXECUTABLE}"
                "--no-compiler-runtime"
                "$<TARGET_FILE:demo_app>"
        )
        # windeployqt only deploys qwindows; also deploy qoffscreen/qminimal so
        # the exe can run headless (QT_QPA_PLATFORM=offscreen) for automated
        # testing. Without them the app aborts at startup ("could not find or
        # load the Qt platform plugin") when offscreen is requested.
        get_filename_component(_qt_platforms_dir "${_qt_bin_dir}/../plugins/platforms" ABSOLUTE)
        add_custom_command(TARGET demo_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:demo_app>/platforms"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_qt_platforms_dir}/qoffscreen$<$<CONFIG:Debug>:d>.dll"
                "${_qt_platforms_dir}/qminimal$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:demo_app>/platforms/"
        )
    else()
        message(WARNING "windeployqt not found near ${_qt_bin_dir}; demo_app will need Qt's bin directory on PATH to run")
    endif()
endif()
