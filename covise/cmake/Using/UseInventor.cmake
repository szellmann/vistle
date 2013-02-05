MACRO(USE_INVENTOR)
  MESSAGE("inventor:")
  MESSAGE(INTENTOR_FOUND)
  IF(NOT INVENTOR_FOUND)
  ENDIF(NOT INVENTOR_FOUND)

  IF ((NOT INVENTOR_FOUND) AND (${ARGC} LESS 1))
    MESSAGE("Skipping because of missing Inventor")
    RETURN()
  ENDIF((NOT INVENTOR_FOUND) AND (${ARGC} LESS 1))
  IF(NOT INVENTOR_USED AND INVENTOR_FOUND)
    SET(INVENTOR_USED TRUE)  
    INCLUDE_DIRECTORIES(${INVENTOR_INCLUDE_DIR})
    SET(EXTRA_LIBS ${EXTRA_LIBS} ${INVENTOR_XT_LIBRARY} ${INVENTOR_IMAGE_LIBRARY} ${INVENTOR_LIBRARY} ${INVENTOR_FL_LIBRARY} ${JPEG_LIBRARY})
    IF(INVENTOR_FREETYPE_LIBRARY)
      SET(EXTRA_LIBS ${EXTRA_LIBS} ${INVENTOR_FREETYPE_LIBRARY})
    ENDIF()
  ENDIF()
ENDMACRO(USE_INVENTOR)
