MACRO(USE_OSGEARTH)
  FIND_PACKAGE(OsgEarth)
  IF (((NOT OSGEARTH_FOUND) OR (NOT OSGEARTHANNOTATION_LIBRARY)) AND (${ARGC} LESS 1))
    MESSAGE("Skipping because of missing OSGEARTH")
    RETURN()
  ENDIF(((NOT OSGEARTH_FOUND) OR (NOT OSGEARTHANNOTATION_LIBRARY)) AND (${ARGC} LESS 1))
  IF(NOT OSGEARTH_USED AND OSGEARTH_FOUND)
    SET(OSGEARTH_USED TRUE)
    USE_OPENGL()
    INCLUDE_DIRECTORIES(${OSGEARTH_INCLUDE_DIR})
    SET(EXTRA_LIBS ${EXTRA_LIBS} ${OSGEARTH_LIBRARIES})
  ENDIF()
ENDMACRO(USE_OSGEARTH)

