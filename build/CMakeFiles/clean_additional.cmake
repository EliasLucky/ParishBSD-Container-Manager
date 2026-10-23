# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles/parish-manager_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/parish-manager_autogen.dir/ParseCache.txt"
  "parish-manager_autogen"
  )
endif()
