#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <netinet/in.h>

#include "../logger.h"
#include "server.h"

namespace po = boost::program_options;
namespace logging = boost::log;
namespace sc = server_configuration;

using std::string;

ServerConfiguration parse_program_arguments(int argc, const char* argv[]) {
    ServerConfiguration server_configuration;
    int64_t timeout;
    int32_t port;

    po::options_description description {"Program options"};
    description.add_options()
            ("help,h", "Help screen")
            ("MCAST_ADDR,g", po::value<string>(&server_configuration.mcast_addr)->required(), "Multicast address")
            ("CMD_PORT,p", po::value<int32_t>(&port)->required()->notifier([](int32_t port) {
                if (port <= 0 || port > UINT16_MAX) {
                    std::cerr << "CMD_PORT out of range\n";
                    exit(1);
                }
            }), "UDP port used for sending and receiving messages")
            ("MAX_SPACE,b", po::value<uint64_t>(&server_configuration.max_space)->default_value(sc::default_max_disc_space),
             ("Maximum disc space in this server node\n"
              "  Default value: " + std::to_string(sc::default_max_disc_space)).c_str())
            ("SHRD_FLDR,f", po::value<string>(&server_configuration.shared_folder)->required(), "Path to the directory in which files are stored")
            ("TIMEOUT,t", po::value<int64_t>(&timeout)->default_value(sc::default_timeout)->notifier([](int64_t timeout) {
                 if (timeout > sc::max_timeout || timeout <= 0) {
                     std::cerr << "TIMEOUT out of range\n";
                     exit(1);
                 }
             }),
             (("Maximum waiting time for connection from client\n"
               "  Min value: 0\n"
               "  Max value: " + std::to_string(sc::max_timeout)) + "\n" +
              "  Default value: " + std::to_string(sc::default_timeout)).c_str());

    po::variables_map var_map;
    try {
        po::store(po::parse_command_line(argc, argv, description), var_map);
        if (var_map.count("help")) {
            std::cout << description << std::endl;
            exit(0);
        }

        po::notify(var_map);
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        std::cout << description << std::endl;
        exit(1);
    }
    server_configuration.timeout = timeout;
    server_configuration.cmd_port = port;

    return server_configuration;
}

void init() {
    logging::register_simple_formatter_factory<logging::trivial::severity_level, char>("Severity");
    logging::add_file_log
            (
                    logging::keywords::file_name = "logger_server.log",
                    logging::keywords::rotation_size = 10 * 1024 * 1024,
                    logging::keywords::time_based_rotation = logging::sinks::file::rotation_at_time_point(0, 0, 0),
                    logging::keywords::format = "[%TimeStamp%] [tid=%ThreadID%] [%Severity%]: %Message%",
                    logging::keywords::auto_flush = true
            );
    logging::add_common_attributes();

    logging::core::get()->set_logging_enabled(false);
}

int main(int argc, const char* argv[]) {
    init();
    ServerConfiguration server_configuration = parse_program_arguments(argc, argv);
    Server server {server_configuration};

    try {
        server.init();
        BOOST_LOG_TRIVIAL(trace) << "Starting server...";
        BOOST_LOG_TRIVIAL(trace) << server << std::endl;
        server.run();
    } catch (const std::exception& e) {
        logger::syserr(e.what());
        exit(1);
    }

    return 0;
}