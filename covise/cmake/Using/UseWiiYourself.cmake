MACRO(USE_WIIYOURSELF)
  FIND_PACKAGE(WiiYourself)
  IF ((NOT WIIYOURSELF_FOUND) AND (${ARGC} LESS 1))
    MESSAGE("Skipping because of missing WIIYOURSELF")
    RETURN()
  ENDIF((NOT WIIYOURSELF_FOUND) AND (${ARGC} LESS 1))
  IF(NOT WIIYOURSELF_USED AND WIIYOURSELF_FOUND)
    SET(WIIYOURSELF_USED TRUE)
    INCLUDE_DIRECTORIES(${WIIYOURSELF_INCLUDE_DIR})
    SET(EXTRA_LIBS ${EXTRA_LIBS} ${WIIYOURSELF_LIBRARIES})
  ENDIF()
ENDMACRO(USE_WIIYOURSELF)
