#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include "client.h"

using std::string;

namespace po = boost::program_options;
namespace logging = boost::log;
namespace cc = client_configuration;


ClientConfiguration parse_program_arguments(int argc, const char* argv[]) {
    ClientConfiguration client_configuration{};
    int64_t timeout;
    int32_t port;

    po::options_description description {"Program options"};
    description.add_options()
            ("help,h", "Help screen")
            ("MCAST_ADDR,g", po::value<string>(&client_configuration.mcast_addr)->required(), "Multicast address")
            ("CMD_PORT,p", po::value<int32_t>(&port)->required()->notifier([](int32_t port) {
                if (port <= 0 || port > UINT16_MAX) {
                    std::cerr << "CMD_PORT out of range\n";
                    exit(1);
                }
            }), "UDP port on which servers are listening")
            ("OUT_FLDR,o", po::value<string>(&client_configuration.out_folder)->required(), "Path to the directory in which files should be saved")
            ("TIMEOUT,t", po::value<int64_t>(&timeout)->default_value(cc::default_timeout)->notifier([](int64_t timeout) {
                 if (timeout > cc::max_timeout) {
                     std::cerr << "TIMEOUT out of range" << std::endl;
                     exit(1);
                 }
             }),
             (("Maximum waiting time for information from servers\n"
               "  Min value: 0\n"
               "  Max value: " + std::to_string(cc::max_timeout)) + "\n" +
              "  Default value: " + std::to_string(cc::default_timeout)).c_str());

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
    client_configuration.cmd_port = port;
    client_configuration.timeout = timeout;

    return client_configuration;
}

void init() {
    logging::register_simple_formatter_factory<logging::trivial::severity_level, char>("Severity");
    logging::add_file_log
            (
                    logging::keywords::file_name = "logger_client.log",
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
    ClientConfiguration configuration = parse_program_arguments(argc, argv);
    Client client {configuration};
    client.init();
    BOOST_LOG_TRIVIAL(trace) << client << std::endl;
    client.run();

    return 0;
}
