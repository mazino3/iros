if (NOT ${NATIVE_BUILD})
    set(OS_LIBS
        loader
    )
endif()

set(OS_LIBS
    ${OS_LIBS}
    libcli
    libdi
    libdius
    libext
    libliim
)

if (NOT ${MINIMAL_BUILD})
    set(OS_LIBS
        ${OS_LIBS}
        libapp
        libc
        libccpp
        libclanguage
        libclipboard
        libedit
        libeventloop
        libgraphics
        libgui
        libipc
        libprocinfo
        librepl
        libsh
        libsharedtest
        libsharedtest2
        libterminal
        libtest
        libthread
        libtinput
        libtui
        libunicode
    )
endif()

set(TARGETS ${OS_LIBS})

add_all_targets()
