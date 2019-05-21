cmake_minimum_required(VERSION 3.14)

project(distributed_files_storage)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost 1.69 COMPONENTS program_options filesystem REQUIRED)
include_directories(${Boost_INCLUDE_DIR})

add_executable(netstore-client netstore-client.cpp err.cpp)
add_executable(netstore-server netstore-server.cpp err.cpp)
add_executable(netstore-server2 netstore-server.cpp err.cpp)

TARGET_LINK_LIBRARIES(netstore-client LINK_PUBLIC ${Boost_LIBRARIES})
TARGET_LINK_LIBRARIES(netstore-server LINK_PUBLIC ${Boost_LIBRARIES})
TARGET_LINK_LIBRARIES(netstore-server2 LINK_PUBLIC ${Boost_LIBRARIES})