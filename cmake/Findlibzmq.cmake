include(LibFindMacros)

libfind_pkg_check_modules(LIBZMQ_PKGCONF LIBZMQ)

find_path(LIBZMQ_INCLUDE_DIR
	NAMES zmq.hpp
	PATHS
		${LIBZMQ_PKGCONF_INCLUDE_DIRS}
		"/usr/include"
		"/usr/local/include"
		"/opt/homebrew/include"
)

find_library(LIBZMQ_LIBRARY
	NAMES libzmq
	PATHS
		${LIBZMQ_PKGCONF_LIBRARY_DIRS}
        "/usr/lib"
        "/usr/lib64"
        "/usr/local/lib"
        "/usr/local/lib64"
		"/opt/homebrew/lib"
)

set(LIBZMQ_PROCESS_INCLUDES LIBZMQ_INCLUDE_DIR)
set(LIBZMQ_PROCESS_LIBS LIBZMQ_LIBRARY)
libfind_process(LIBZMQ)
message("ABC")
