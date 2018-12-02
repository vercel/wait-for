project (wait-for)
cmake_minimum_required (VERSION 3.2)

include (CheckSymbolExists)
check_symbol_exists (strdup string.h HAVE_STRDUP)

configure_file ("${PROJECT_SOURCE_DIR}/config.h.in"	"${PROJECT_BINARY_DIR}/config.h")
include_directories ("${PROJECT_BINARY_DIR}")

add_subdirectory (ext/xopt)

if (APPLE OR UNIX)
	set (CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -pedantic -std=c99")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Og")
	set (CMAKE_C_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Ofast")
	set (CMAKE_EXE_LINKER_FLAGS_RELEASE "-static-libgcc -static")
endif ()

add_executable (wait-for wait-for.c strdup.c)
target_link_libraries (wait-for xopt)
