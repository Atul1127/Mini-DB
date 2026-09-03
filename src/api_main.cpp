#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "ApiServer.hpp"
#include "Database.hpp"
#include "Logger.hpp"

namespace {

unsigned short parsePort(int argc, char* argv[]) {
    unsigned long parsedPort = 18080;
    if (argc >= 2) {
        try {
            parsedPort = std::stoul(argv[1]);
        } catch (const std::exception&) {
            std::cerr << "Invalid port '" << argv[1] << "'. Using 18080.\n";
            parsedPort = 18080;
        }
    }

    if (parsedPort == 0 || parsedPort > 65535) {
        std::cerr << "Port out of range. Using 18080.\n";
        parsedPort = 18080;
    }

    return static_cast<unsigned short>(parsedPort);
}

}  // namespace

int main(int argc, char* argv[]) {
    Logger logger("logs/mini-db.log");
    Database database;
    std::string loadError;
    if (!database.loadFromDisk("data", loadError)) {
        logger.error("API startup load failed: " + loadError);
        std::cerr << "Warning: failed to load persisted data: " << loadError << '\n';
    } else {
        logger.info("API startup load completed.");
    }

    ApiServer server(database, "data");
    server.start(parsePort(argc, argv));
    return EXIT_SUCCESS;
}
