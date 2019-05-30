#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <csignal>

#include "server.h"

namespace po = boost::program_options;
namespace logging = boost::log;
namespace sc = server_configuration;

using std::cout;
using std::endl;
using std::cerr;
using std::string;


/**
 * Use it correctly first time!
 * @param server_config
 * @return
 */
Server& get_server(ServerConfiguration* server_config = nullptr) {
    static Server server {*server_config};
    return server;
}

static void exit_procedure(int sig) {
    cout << "Wciśnięto CTRL+C" << endl;
    get_server().stop();
    std::mutex exit_mutex{};
    std::condition_variable running_threads_cv;

    std::unique_lock<std::mutex> lock(exit_mutex);
    running_threads_cv.wait(lock, [] {
        return get_server().no_threads_running();
    });
    cout << "No more threads!" << endl;
    //exit(0); // TODO raczej to server powinien się wyłączyć...
}


void init() {
    std::signal(SIGINT, exit_procedure);
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

    //  logging::core::get()->set_logging_enabled(false);
}

ServerConfiguration parse_program_arguments(int argc, const char *argv[]) {
    ServerConfiguration server_configuration;

    po::options_description description {"Program options"};
    description.add_options()
            ("help,h", "Help screen")
            ("MCAST_ADDR,g", po::value<string>(&server_configuration.mcast_addr)->required(), "Multicast address")
            ("CMD_PORT,p", po::value<in_port_t>(&server_configuration.cmd_port)->required(), "UDP port used for sending and receiving messages")
            ("MAX_SPACE,b", po::value<uint64_t>(&server_configuration.max_space)->default_value(sc::default_max_disc_space),
             ("Maximum disc space in this server node\n"
              "  Default value: " + std::to_string(sc::default_max_disc_space)).c_str())
            ("SHRD_FLDR,f", po::value<string>(&server_configuration.shared_folder)->required(), "Path to the directory in which files are stored")
            ("TIMEOUT,t", po::value<uint16_t>(&server_configuration.timeout)->default_value(sc::default_timeout)->notifier([](uint16_t timeout) {
                 if (timeout > sc::max_timeout) {
                     cerr << "TIMEOUT out of range\n";
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
            cout << description << endl;
            exit(0);
        }

        po::notify(var_map);
    } catch (po::required_option &e) {
        cerr << e.what() << endl;
        exit(1);
    }

    return server_configuration;
}


int main(int argc, const char *argv[]) {
    init();
    ServerConfiguration server_configuration = parse_program_arguments(argc, argv);
    get_server(&server_configuration);

    try {
        get_server().init();
        BOOST_LOG_TRIVIAL(trace) << "Starting server...";
        BOOST_LOG_TRIVIAL(trace) << get_server() << endl;
        get_server().run();
    } catch (const std::exception& e) {
        cerr << e.what() << endl;
        return 1;
    }

    return 0;
}