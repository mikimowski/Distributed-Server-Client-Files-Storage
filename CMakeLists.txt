cmake_minimum_required(VERSION 3.14)

project(distributed_files_storage)

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DBOOST_LOG_DYN_LINK)

find_package(Boost 1.65 COMPONENTS program_options log filesystem REQUIRED)
include_directories(${Boost_INCLUDE_DIR})

#add_executable(netstore-client1 client.cpp err.cpp helper.cpp)
#add_executable(netstore-client2 client.cpp err.cpp helper.cpp)
#add_executable(netstore-server2 netstore_server.cpp server.cpp err.cpp)
add_executable(tmp communication_protocol.cpp helper.cpp server.cpp)
add_executable(s server.cpp server.h helper.h communication_protocol.h err.h)
add_executable(helper helper.cpp err.cpp)
add_executable(communication_protocol communication_protocol.cpp helper.cpp err.cpp)
add_executable(server server.cpp communication_protocol.cpp helper.cpp err.cpp)
add_executable(netstore-server1 netstore_server.cpp server.cpp tcp_socket.cpp udp_socket.cpp inet_socket.cpp communication_protocol.cpp helper.cpp err.cpp)
add_executable(netstore-client1 netstore_client.cpp client.cpp communication_protocol.cpp helper.cpp err.cpp)
add_executable(inet inet_socket.cpp tcp_socket.cpp udp_socket.cpp)

target_link_libraries(netstore-server1 Boost::filesystem Boost::log -lpthread Boost::program_options)
target_link_libraries(netstore-client1 Boost::filesystem Boost::log -lpthread Boost::program_options)
#target_link_libraries(netstore-server2 Boost::filesystem Boost::log -lboost_log_setup -lpthread Boost::program_options)
#target_link_libraries(netstore-client1 Boost::filesystem Boost::log -lpthread Boost::program_options)
#target_link_libraries(netstore-client2 Boost::filesystem Boost::log -lpthread Boost::program_options)
#TARGET_LINK_LIBRARIES(netstore-client LINK_PUBLIC ${Boost_LIBRARIES})
#TARGET_LINK_LIBRARIES(netstore-server1 LINK_PUBLIC ${Boost_LIBRARIES})
#TARGET_LINK_LIBRARIES(netstore-server2 LINK_PUBLIC -lpthread  ${Boost_LIBRARIES})

target_link_libraries(helper Boost::filesystem Boost::log -lpthread Boost::program_options)
target_link_libraries(communication_protocol Boost::filesystem Boost::log -lpthread Boost::program_options)
target_link_libraries(server Boost::filesystem Boost::log -lpthread Boost::program_options)