MACRO(USE_OSC)
  FIND_PACKAGE(OSC)
  IF ((NOT OSC_FOUND) AND (${ARGC} LESS 1))
    MESSAGE("Skipping because of missing OSC")
    RETURN()
  ENDIF((NOT OSC_FOUND) AND (${ARGC} LESS 1))
  IF(NOT OSC_USED AND OSC_FOUND)
    SET(OSC_USED TRUE)
    INCLUDE_DIRECTORIES(${OSC_INCLUDE_DIR})
    SET(EXTRA_LIBS ${EXTRA_LIBS} ${OSC_LIBRARIES})
  ENDIF()
ENDMACRO(USE_OSC)
