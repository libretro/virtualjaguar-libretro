# VJPackage.cmake -- install rules + release-build switches for packaging (#839).
#
# Included at the end of app/CMakeLists.txt.  Everything here is inert for a
# plain `cmake --build`; it only adds `cmake --install` content and, on request,
# Windows link/subsystem settings.
#
# THE INSTALL TREE.  The app finds its bundled data next to the executable
# (SDL_GetBasePath()): cheats/ (#835), libretro-db/ (#838), shaders/ (#834) and the
# optional librashader runtime.  Those features stage their data into the build
# directory beside the executable (some also install() it themselves; some only
# POST_BUILD-copy it).  So that packaging does not have to know which is which,
# `cmake --install` here sweeps the build directory's "beside the executable" payload
# into <prefix>/bin/ generically:
#
#   VJ_PACKAGE_DATA_DIRS   directories beside the executable to install (default
#                          cheats libretro-db shaders).  A new bundled-data feature adds
#                          its directory name here, once, and every package gets it.
#   VJ_PACKAGE_RUNTIME     runtime libraries beside the executable (librashader.*).
#
# Layout produced:   <prefix>/bin/virtualjaguar[.exe]
#                    <prefix>/bin/{cheats,libretro-db,shaders}/ , librashader.*
#                    <prefix>/share/doc/virtualjaguar/{LICENSE.txt,THIRD-PARTY-NOTICES.txt,README.md}
#                    Linux also: share/applications, share/icons/hicolor, share/metainfo, share/mime/packages
# The platform packagers (app/packaging/{macos,linux,windows}) relocate bin/ as the
# platform wants (Resources/ in a .app, /usr/lib/virtualjaguar in a .deb, ...).
#
# Release-build switches:
#   VJ_APP_WIN32_GUI   (Windows) link as a GUI-subsystem program: no console window
#                      when double-clicked.  A small start-up hook re-attaches to the
#                      parent console so `virtualjaguar --help` and ctest still print.
#   VJ_APP_STATIC_RUNTIME (MinGW) link libstdc++/libgcc/winpthread statically, so the
#                      exe needs no MinGW DLLs beside it.  Default ON for MinGW.

set(VJ_PACKAGE_DATA_DIRS cheats libretro-db shaders CACHE STRING
    "Directories beside the executable (in the build tree) that packages ship")
set(VJ_PACKAGE_RUNTIME librashader.dll librashader.dylib "librashader.so*" CACHE STRING
    "Runtime libraries beside the executable (in the build tree) that packages ship")

set(VJ_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Version stamped into package metadata (appstream); the app itself reports PROJECT_VERSION")
option(VJ_APP_WIN32_GUI "Windows: GUI subsystem (no console window)" OFF)
if(MINGW)
    option(VJ_APP_STATIC_RUNTIME "MinGW: link the C/C++ runtime statically" ON)
else()
    set(VJ_APP_STATIC_RUNTIME OFF)
endif()

set(_VJP_DIR "${CMAKE_CURRENT_LIST_DIR}")
if(WIN32)
    enable_language(RC)   # directory scope: not allowed inside a function
endif()

function(vj_package_setup)
    # ---- Windows: icon + version resource, subsystem, static runtime ---------------
    if(WIN32)
        set(VJ_ICO "${_VJP_DIR}/../packaging/icons/virtualjaguar.ico")
        get_filename_component(VJ_ICO "${VJ_ICO}" ABSOLUTE)
        set(VJ_RC_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
        # Application manifest (#833): per-monitor-v2 DPI awareness, long paths, Windows 10/11 compatibility.
        # MSVC embeds a manifest at link time from a .manifest source (it would clash with one in the .rc);
        # MinGW's windres embeds it from the .rc.
        get_filename_component(VJ_MANIFEST "${_VJP_DIR}/../packaging/windows/virtualjaguar.manifest" ABSOLUTE)
        if(MSVC)
            set(VJ_RC_MANIFEST_LINE "")
            target_sources(virtualjaguar PRIVATE "${VJ_MANIFEST}")
        else()
            set(VJ_RC_MANIFEST_LINE "1 24 \"${VJ_MANIFEST}\"")
        endif()
        configure_file("${_VJP_DIR}/../packaging/windows/virtualjaguar.rc.in"
                       "${CMAKE_BINARY_DIR}/virtualjaguar.rc" @ONLY)
        set_source_files_properties("${CMAKE_BINARY_DIR}/virtualjaguar.rc" PROPERTIES
            OBJECT_DEPENDS "${VJ_ICO};${VJ_MANIFEST}")
        target_sources(virtualjaguar PRIVATE "${CMAKE_BINARY_DIR}/virtualjaguar.rc")
        if(VJ_APP_WIN32_GUI)
            target_sources(virtualjaguar PRIVATE
                "${_VJP_DIR}/../packaging/windows/console_attach.cpp")
            if(MSVC)
                target_link_options(virtualjaguar PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
            else()
                target_link_options(virtualjaguar PRIVATE -mwindows)
            endif()
        endif()
        if(VJ_APP_STATIC_RUNTIME)
            target_link_options(virtualjaguar PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()

    # ---- install -----------------------------------------------------------------------
    # Identical to the rule #835 adds; installing a file twice to the same place is harmless.
    install(TARGETS virtualjaguar RUNTIME DESTINATION bin)

    # Sweep the "beside the executable" payload (see the header comment).
    install(CODE "
        set(_vj_src \"$<TARGET_FILE_DIR:virtualjaguar>\")
        set(_vj_dst \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
        foreach(_n ${VJ_PACKAGE_DATA_DIRS})
            if(IS_DIRECTORY \"\${_vj_src}/\${_n}\")
                message(STATUS \"VJPackage: bundling \${_n}/\")
                file(INSTALL \"\${_vj_src}/\${_n}\" DESTINATION \"\${_vj_dst}\" USE_SOURCE_PERMISSIONS)
            endif()
        endforeach()
        foreach(_g ${VJ_PACKAGE_RUNTIME})
            file(GLOB _hits \"\${_vj_src}/\${_g}\")
            foreach(_h IN LISTS _hits)
                message(STATUS \"VJPackage: bundling runtime \${_h}\")
                file(INSTALL \"\${_h}\" DESTINATION \"\${_vj_dst}\" USE_SOURCE_PERMISSIONS)
            endforeach()
        endforeach()
    ")

    # Licence texts and notices travel with every package.
    set(VJ_REPO_ROOT "${_VJP_DIR}/../..")
    install(FILES "${VJ_REPO_ROOT}/LICENSE"
            DESTINATION share/doc/virtualjaguar RENAME LICENSE.txt)
    install(FILES "${_VJP_DIR}/../packaging/THIRD-PARTY-NOTICES.txt"
            DESTINATION share/doc/virtualjaguar)
    install(FILES "${_VJP_DIR}/../README.md"
            DESTINATION share/doc/virtualjaguar)

    # ---- Linux desktop integration (deb, AppImage, Flatpak all share these) --------------
    if(UNIX AND NOT APPLE)
        set(VJ_APP_ID "io.github.libretro.VirtualJaguar")
        install(FILES "${_VJP_DIR}/../packaging/linux/${VJ_APP_ID}.desktop"
                DESTINATION share/applications)
        # File types (#833): shared-mime-info reads share/mime/packages; the .deb's dpkg trigger, Flatpak's
        # export and a distro package's post-install run update-mime-database.
        install(FILES "${_VJP_DIR}/../packaging/linux/${VJ_APP_ID}.xml"
                DESTINATION share/mime/packages)
        string(TIMESTAMP VJ_PACKAGE_DATE "%Y-%m-%d" UTC)
        configure_file("${_VJP_DIR}/../packaging/linux/${VJ_APP_ID}.metainfo.xml"
                       "${CMAKE_BINARY_DIR}/${VJ_APP_ID}.metainfo.xml" @ONLY)
        install(FILES "${CMAKE_BINARY_DIR}/${VJ_APP_ID}.metainfo.xml"
                DESTINATION share/metainfo)
        foreach(_s 16 32 48 64 128 256 512)
            install(FILES "${_VJP_DIR}/../packaging/icons/png/virtualjaguar-${_s}.png"
                    DESTINATION share/icons/hicolor/${_s}x${_s}/apps RENAME ${VJ_APP_ID}.png)
        endforeach()
        install(FILES "${_VJP_DIR}/../packaging/icons/virtualjaguar.svg"
                DESTINATION share/icons/hicolor/scalable/apps RENAME ${VJ_APP_ID}.svg)
    endif()
endfunction()

# Run once the whole of app/CMakeLists.txt (and everything it include()s) has been read.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL vj_package_setup)
