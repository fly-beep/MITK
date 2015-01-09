find_package(ACVD REQUIRED CONFIG)

foreach(_lib ${ACVD_LIBRARIES})
  list(APPEND ALL_LIBRARIES "${ACVD_LIBRARY_DIRS}/${CMAKE_SHARED_LIBRARY_PREFIX}${_lib}${CMAKE_SHARED_LIBRARY_SUFFIX}")
endforeach()
list(APPEND ALL_INCLUDE_DIRECTORIES ${ACVD_INCLUDE_DIRS})
