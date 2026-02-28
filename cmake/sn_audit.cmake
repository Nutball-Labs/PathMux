# sn_audit.cmake — called by the sn-audit custom target
# Usage: cmake -DSRC=<source_dir> -P sn_audit.cmake

if(NOT DEFINED SRC)
    message(FATAL_ERROR "SRC not defined")
endif()

set(OUTFILE "${SRC}/sn_audit.txt")

# Gather source files
file(GLOB CPP_FILES "${SRC}/*.cpp")
file(GLOB HPP_FILES "${SRC}/*.hpp")
set(ALL_SRC ${CPP_FILES} ${HPP_FILES})

# Non-code files that also carry SN comments
foreach(EXTRA
    "${SRC}/CMakeLists.txt"
    "${SRC}/pathmux_project_brief.md"
)
    if(EXISTS "${EXTRA}")
        list(APPEND ALL_SRC "${EXTRA}")
    endif()
endforeach()

string(TIMESTAMP NOW "%Y-%m-%d %H:%M:%S")
file(WRITE "${OUTFILE}" "--- PathMux SN Audit: ${NOW} ---\n")

foreach(F ${ALL_SRC})
    get_filename_component(FNAME "${F}" NAME)
    file(STRINGS "${F}" LINES)
    foreach(LINE ${LINES})
        if(LINE MATCHES "^(//|#) SN: ([0-9]+)")
            # Pad filename to 35 chars
            string(LENGTH "${FNAME}" FLEN)
            set(SPACES "")
            if(FLEN LESS 35)
                math(EXPR PADLEN "35 - ${FLEN}")
                string(REPEAT " " ${PADLEN} SPACES)
            endif()
            file(APPEND "${OUTFILE}" "${FNAME}${SPACES}${CMAKE_MATCH_2}\n")
        endif()
    endforeach()
endforeach()

message(STATUS "SN Audit written to ${OUTFILE}")
