include(CMakeParseArguments)
include(ProcessorCount)

function(GenerateUnity)
    set(_OPTIONS_ARGS)
    set(_ONE_VALUE_ARGS ORIGINAL_SOURCES NUMBER_UNITS UNITY_PREFIX)
    set(_MULTI_VALUE_ARGS)

    cmake_parse_arguments(_GENERATEUNITY "${_OPTIONS_ARGS}" "${_ONE_VALUE_ARGS}" "${_MULTI_VALUE_ARGS}" ${ARGN})

    if(NOT _GENERATEUNITY_ORIGINAL_SOURCES)
        message(FATAL_ERROR "GenerateUnity: 'ORIGINAL_SOURCES' argument required" )
    endif()

    if(NOT _GENERATEUNITY_NUMBER_UNITS)
        ProcessorCount(CORES)
        if(NOT CORES EQUAL 0)
            set(_GENERATEUNITY_NUMBER_UNITS ${CORES})
        else()
            set(_GENERATEUNITY_NUMBER_UNITS 1)
        endif()
    endif()

    if(_GENERATEUNITY_UNITY_PREFIX)
        set(_GENERATEUNITY_UNITY_PREFIX "${_GENERATEUNITY_UNITY_PREFIX}_")
    endif()

    math(EXPR LAST_UNIT_INDEX "${_GENERATEUNITY_NUMBER_UNITS} - 1")

    list(LENGTH ${_GENERATEUNITY_ORIGINAL_SOURCES} NUMBER_FILES)
    math(EXPR MIN_FILES_PER_UNIT "${NUMBER_FILES} / ${_GENERATEUNITY_NUMBER_UNITS}")
    math(EXPR LEFT_OVER_FILES "${NUMBER_FILES} % ${_GENERATEUNITY_NUMBER_UNITS}")

    unset(LIST_NUMBER_FILES_IN_UNIT)
    foreach(INDEX RANGE ${LAST_UNIT_INDEX})
        if((NUMBER_FILES LESS 0) OR (NUMBER_FILES EQUAL 0))
            break()
        endif()

        set(NUMBER_FILES_IN_UNIT ${MIN_FILES_PER_UNIT})
        if(LEFT_OVER_FILES GREATER 0)
            math(EXPR LEFT_OVER_FILES "${LEFT_OVER_FILES} - 1")
            math(EXPR NUMBER_FILES_IN_UNIT "${NUMBER_FILES_IN_UNIT} + 1")
        endif()

        math(EXPR NUMBER_FILES "${NUMBER_FILES} - ${NUMBER_FILES_IN_UNIT}")

        list(APPEND LIST_NUMBER_FILES_IN_UNIT ${NUMBER_FILES_IN_UNIT})
    endforeach(INDEX)

    # Reset LAST_UNIT_INDEX in case NUMBER_FILES is less than the number of units
    list(LENGTH LIST_NUMBER_FILES_IN_UNIT _GENERATEUNITY_NUMBER_UNITS)
    math(EXPR LAST_UNIT_INDEX "${_GENERATEUNITY_NUMBER_UNITS} - 1")

    unset(NEW_SOURCES)
    foreach(INDEX RANGE ${LAST_UNIT_INDEX})
        set(UNIT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/${_GENERATEUNITY_UNITY_PREFIX}unity${INDEX}.cpp")
        list(APPEND NEW_SOURCES ${UNIT_FILE_NAME})

        list(GET LIST_NUMBER_FILES_IN_UNIT ${INDEX} NUMBER_FILES)
        math(EXPR LAST_FILE_INDEX "${NUMBER_FILES} - 1")

        unset(UNIT_ORIGINAL_SOURCES)
        foreach(FILE_INDEX RANGE ${LAST_FILE_INDEX})
            list(GET ${_GENERATEUNITY_ORIGINAL_SOURCES} 0 SOURCE_FILE)
            list(REMOVE_AT ${_GENERATEUNITY_ORIGINAL_SOURCES} 0)
            list(APPEND UNIT_ORIGINAL_SOURCES ${SOURCE_FILE})
        endforeach(FILE_INDEX)

        file(WRITE ${UNIT_FILE_NAME} "// Generated by unity.cmake\n")
        foreach(UNIT_ORIGINAL_SOURCE ${UNIT_ORIGINAL_SOURCES})
            file(APPEND ${UNIT_FILE_NAME} "#include \"${UNIT_ORIGINAL_SOURCE}\"\n")
        endforeach(UNIT_ORIGINAL_SOURCE)
    endforeach(INDEX)

    set(${_GENERATEUNITY_ORIGINAL_SOURCES} ${NEW_SOURCES} PARENT_SCOPE)
endfunction()