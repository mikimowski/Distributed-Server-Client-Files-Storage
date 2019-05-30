#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <csignal>

#include "client.h"

using std::string;
using std::cout;
using std::endl;
using std::cerr;

namespace po = boost::program_options;
namespace logging = boost::log;
namespace cc = client_configuration;


ClientConfiguration parse_program_arguments(int argc, const char* argv[]) {
    ClientConfiguration client_configuration{};

    po::options_description description {"Program options"};
    description.add_options()
            ("help,h", "Help screen")
            ("MCAST_ADDR,g", po::value<string>(&client_configuration.mcast_addr)->required(), "Multicast address")
            ("CMD_PORT,p", po::value<uint16_t>(&client_configuration.cmd_port)->required(), "UDP port on which servers are listening")
            ("OUT_FLDR,o", po::value<string>(&client_configuration.out_folder)->required(), "Path to the directory in which files should be saved")
            ("TIMEOUT,t", po::value<uint16_t>(&client_configuration.timeout)->default_value(cc::default_timeout)->notifier([](uint16_t timeout) {
                 if (timeout > cc::max_timeout) {
                     cerr << "TIMEOUT out of range\n";
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
            cout << description << endl;
            exit(0);
        }
        po::notify(var_map);
    } catch (po::required_option &e) {
        cerr << e.what() << endl;
        exit(1);
    }

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
}


int main(int argc, const char* argv[]) {
    init();

    ClientConfiguration configuration = parse_program_arguments(argc, argv);
    Client client {configuration};
    client.init();
    BOOST_LOG_TRIVIAL(trace) << client << endl;
    client.run();

    return 0;
}
