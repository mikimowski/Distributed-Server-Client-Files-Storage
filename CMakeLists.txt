cmake_minimum_required(VERSION 3.14)

project(distributed_files_storage)

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DBOOST_LOG_DYN_LINK)

find_package(Boost 1.65 COMPONENTS program_options log filesystem REQUIRED)
include_directories(${Boost_INCLUDE_DIR})

add_executable(netstore-server server/netstore_server.cpp server/server.cpp logger.cpp socket/tcp_socket.cpp socket/udp_socket.cpp socket/inet_socket.cpp communication_protocol.cpp helper.cpp)
add_executable(netstore-server2 server/netstore_server.cpp server/server.cpp logger.cpp socket/tcp_socket.cpp socket/udp_socket.cpp socket/inet_socket.cpp communication_protocol.cpp helper.cpp)
add_executable(netstore-client client/netstore_client.cpp client/client.cpp logger.cpp socket/tcp_socket.cpp socket/udp_socket.cpp socket/inet_socket.cpp communication_protocol.cpp helper.cpp)

target_link_libraries(netstore-server Boost::filesystem Boost::log -lpthread Boost::program_options)
target_link_libraries(netstore-server2 Boost::filesystem Boost::log -lpthread Boost::program_options)
target_link_libraries(netstore-client Boost::filesystem Boost::log -lpthread Boost::program_options)