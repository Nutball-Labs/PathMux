# archive.cmake — called at build time by the archive custom target.
# Reads version.hpp fresh so the archive name is always correct even if
# cmake .. hasn't been re-run since the last version bump.
#
# Called with:
#   cmake -DSRC=<source_root> -P cmake/archive.cmake

if(NOT DEFINED SRC)
    message(FATAL_ERROR "archive.cmake: SRC not defined")
endif()

# --- Read version.hpp -------------------------------------------------------
file(READ "${SRC}/lib/version.hpp" VER_HPP)

string(REGEX MATCH "VERSION_MAJOR ([0-9]+)" _ "${VER_HPP}")
set(VER_MAJOR ${CMAKE_MATCH_1})

string(REGEX MATCH "VERSION_MINOR ([0-9]+)" _ "${VER_HPP}")
set(VER_MINOR ${CMAKE_MATCH_1})

string(REGEX MATCH "VERSION_PATCH ([0-9]+)" _ "${VER_HPP}")
set(VER_PATCH ${CMAKE_MATCH_1})

string(REGEX MATCH "VERSION_SUFFIX \"([^\"]*)\"" _ "${VER_HPP}")
set(VER_SUFFIX ${CMAKE_MATCH_1})

set(VERSION "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}${VER_SUFFIX}")

# --- Create archive/ dir ----------------------------------------------------
file(MAKE_DIRECTORY "${SRC}/archive")

set(ARCHIVE "${SRC}/archive/${VERSION}.tar")

# --- Build file list from archive_files.txt ---------------------------------
file(READ "${SRC}/cmake/archive_files.txt" FILES_RAW)
string(REGEX REPLACE "\r?\n" ";" FILE_LIST "${FILES_RAW}")

set(ABS_FILES)
foreach(F ${FILE_LIST})
    string(STRIP "${F}" F)
    if(NOT F STREQUAL "")
        list(APPEND ABS_FILES "${SRC}/${F}")
    endif()
endforeach()

# --- Write tarball ----------------------------------------------------------
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar cf "${ARCHIVE}" ${ABS_FILES}
    WORKING_DIRECTORY "${SRC}"
    RESULT_VARIABLE TAR_RESULT
)

if(NOT TAR_RESULT EQUAL 0)
    message(FATAL_ERROR "archive.cmake: tar failed (exit ${TAR_RESULT})")
endif()

message(STATUS "Archived source to archive/${VERSION}.tar")
