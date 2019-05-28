cmake_minimum_required(VERSION 3.14)

project(distributed_files_storage)

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DBOOST_LOG_DYN_LINK)

find_package(Boost 1.65 COMPONENTS program_options log filesystem REQUIRED)
include_directories(${Boost_INCLUDE_DIR})

add_executable(netstore-client client.cpp err.cpp helper.cpp)
add_executable(netstore-server1 server.cpp err.cpp helper.cpp)
add_executable(netstore-server2 server.cpp err.cpp helper.cpp)

target_link_libraries(netstore-server1 Boost::filesystem Boost::log -lpthread Boost::program_options)
#target_link_libraries(netstore-server2 Boost::filesystem Boost::log -lboost_log_setup -lpthread Boost::program_options)
target_link_libraries(netstore-client Boost::filesystem Boost::log -lpthread Boost::program_options)
#TARGET_LINK_LIBRARIES(netstore-client LINK_PUBLIC ${Boost_LIBRARIES})
#TARGET_LINK_LIBRARIES(netstore-server1 LINK_PUBLIC ${Boost_LIBRARIES})
TARGET_LINK_LIBRARIES(netstore-server2 LINK_PUBLIC -lpthread  ${Boost_LIBRARIES})