# CMakeList.txt : CMake project for DepConvolver, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

#SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -funsafe-math-optimizations -mfpu=neon")
#SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -funsafe-math-optimizations")

project ("DepPlayer")
include_directories("../sw__dep_examples/dep_audio_buffers/dep_audio_buffers/include")
# Add source to this project's executable.
add_executable (DepConvolver "DepPlayer.cpp" 
                             "../sw__dep_examples/dep_audio_buffers/dep_audio_buffers/src/DanteBuffers.cpp" 
                             "../sw__dep_examples/dep_audio_buffers/dep_audio_buffers/src/posix/DanteSharedMemory.cpp"  
                             "../sw__dep_examples/dep_audio_buffers/dep_audio_buffers/src/posix/DanteTiming.cpp" 
                             "../sw__dep_examples/dep_audio_buffers/dep_audio_buffers/src/DantePriority.cpp" )
target_link_libraries(DepConvolver "rt" "pthread")


set_property(TARGET DepConvolver PROPERTY CXX_STANDARD 17)
set_property(TARGET DepConvolver PROPERTY CXX_STANDARD_REQUIRED ON)
 
# TODO: Add tests and install targets if needed.
