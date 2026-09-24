add_executable(unmodified_app main.cpp)

# Strictly links standard Qt Widgets ONLY — zero QtMcp headers, libraries, or references.
target_link_libraries(unmodified_app PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(WIN32)
    get_target_property(_qmake_executable Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET unmodified_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E
                env PATH="${_qt_bin_dir}"
                "${WINDEPLOYQT_EXECUTABLE}"
                "--no-compiler-runtime"
                "$<TARGET_FILE:unmodified_app>"
        )
        get_filename_component(_qt_platforms_dir "${_qt_bin_dir}/../plugins/platforms" ABSOLUTE)
        add_custom_command(TARGET unmodified_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:unmodified_app>/platforms"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_qt_platforms_dir}/qoffscreen$<$<CONFIG:Debug>:d>.dll"
                "${_qt_platforms_dir}/qminimal$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:unmodified_app>/platforms/"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_qt_bin_dir}/Qt${QT_VERSION_MAJOR}Network$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:unmodified_app>/"
        )
    endif()
endif()
