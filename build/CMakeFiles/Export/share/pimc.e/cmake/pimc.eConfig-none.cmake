#----------------------------------------------------------------
# Generated CMake target import file for configuration "None".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "pimc.e" for configuration "None"
set_property(TARGET pimc.e APPEND PROPERTY IMPORTED_CONFIGURATIONS NONE)
set_target_properties(pimc.e PROPERTIES
  IMPORTED_LOCATION_NONE "${_IMPORT_PREFIX}/bin/pimc.e"
  )

list(APPEND _IMPORT_CHECK_TARGETS pimc.e )
list(APPEND _IMPORT_CHECK_FILES_FOR_pimc.e "${_IMPORT_PREFIX}/bin/pimc.e" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
