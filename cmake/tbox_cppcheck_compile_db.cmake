# Writes a copy of the compile database for cppcheck with one entry per
# source file. The library compiles each source once (tbox_objects), but the
# app_lifecycle test compiles src/app/tbox_app.c and tbox_app_events.c again
# with its own flags; cppcheck treats each entry as a separate configuration
# and would analyse those files twice. The first entry for a file wins.
#
#   cmake -DINPUT=<compile_commands.json> -DOUTPUT=<filtered.json> -P <this>

file(READ "${INPUT}" db)
string(JSON count LENGTH "${db}")
math(EXPR last "${count} - 1")

set(seen "")
set(entries "")
foreach(i RANGE ${last})
    string(JSON source GET "${db}" ${i} file)
    if(source IN_LIST seen)
        continue()
    endif()
    list(APPEND seen "${source}")
    string(JSON entry GET "${db}" ${i})
    if(entries STREQUAL "")
        set(entries "${entry}")
    else()
        string(APPEND entries ",\n${entry}")
    endif()
endforeach()

file(WRITE "${OUTPUT}.tmp" "[\n${entries}\n]\n")
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
