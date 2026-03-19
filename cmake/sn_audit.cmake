# sn_audit.cmake — called by the sn-audit custom target
# Usage: cmake -DSRC=<source_dir> -P sn_audit.cmake

if(NOT DEFINED SRC)
    message(FATAL_ERROR "SRC not defined")
endif()

set(OUTFILE "${SRC}/sn_audit.txt")

# Gather source files from all subdirectories
file(GLOB LIB_CPP   "${SRC}/lib/*.cpp")
file(GLOB LIB_HPP   "${SRC}/lib/*.hpp")
file(GLOB CLI_CPP   "${SRC}/cli/*.cpp")
file(GLOB CLI_HPP   "${SRC}/cli/*.hpp")
file(GLOB TOOLS_CPP "${SRC}/tools/*.cpp")
file(GLOB TOOLS_HPP "${SRC}/tools/*.hpp")

file(GLOB MD_FILES    "${SRC}/*.md")
file(GLOB CMAKE_FILES "${SRC}/cmake/*.cmake")

set(ALL_SRC
    ${LIB_CPP} ${LIB_HPP}
    ${CLI_CPP} ${CLI_HPP}
    ${TOOLS_CPP} ${TOOLS_HPP}
    ${MD_FILES}
    ${CMAKE_FILES}
)

# Non-code files that also carry SN comments
foreach(EXTRA
    "${SRC}/CMakeLists.txt"
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
    # Use only the last SN match — avoids false hits from doc examples in .md files
    set(FOUND_SN "")
    foreach(LINE ${LINES})
        if(LINE MATCHES "^(//|#|<!--) SN: ([0-9]+)")
            set(FOUND_SN "${CMAKE_MATCH_2}")
        endif()
    endforeach()
    if(NOT FOUND_SN STREQUAL "")
        # Pad filename to 35 chars
        string(LENGTH "${FNAME}" FLEN)
        set(SPACES "")
        if(FLEN LESS 35)
            math(EXPR PADLEN "35 - ${FLEN}")
            string(REPEAT " " ${PADLEN} SPACES)
        endif()
        file(APPEND "${OUTFILE}" "${FNAME}${SPACES}${FOUND_SN}\n")
    endif()
endforeach()

message(STATUS "SN Audit written to ${OUTFILE}")

# SN: 00087
