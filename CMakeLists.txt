# CMakeList.txt : CMake project for DepConvolver, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

#SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -funsafe-math-optimizations -mfpu=neon")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -funsafe-math-optimizations")

project ("DepConvolver")
add_subdirectory(sw__ats_core)
include_directories("../dep/lib/include" "../code" "sw__ats_core/ats_core/include" "sw__ats_core/chrono/include")
# Add source to this project's executable.
add_executable (DepConvolver "DepConvolver.cpp" "../dep/lib/src/DanteBuffers.cpp" "../dep/lib/src/DanteSharedMemory.cpp" "../dep/lib/src/DanteTiming.cpp" "../dep/lib/src/DantePriority.cpp" "../dep/lib/src/DanteRunner.cpp" "../code/ThreadedDSP.cpp")
target_link_libraries(DepConvolver "rt" "pthread" ats)


#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()

if (CMAKE_CXX_LIBRARY_ARCHITECTURE STREQUAL x86_64-linux-gnu)
    target_link_libraries(DepConvolver "/home/g/git/rnd__glenn/extern/ipp/lib/x64/libipps.a")
    target_link_libraries(DepConvolver "/home/g/git/rnd__glenn/extern/ipp/lib/x64/libippcore.a")
    target_link_libraries(DepConvolver "/home/g/git/rnd__glenn/extern/ipp/lib/x64/libippvm.a")
ELSE()
    target_link_libraries(DepConvolver "/home/g/git/Ne10/build/modules/libNE10.a")
ENDIF()
set_property(TARGET DepConvolver PROPERTY CXX_STANDARD 17)
set_property(TARGET DepConvolver PROPERTY CXX_STANDARD_REQUIRED ON)
 
# TODO: Add tests and install targets if needed.
