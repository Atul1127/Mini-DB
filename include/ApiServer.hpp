#ifndef API_SERVER_HPP
#define API_SERVER_HPP

#include <string>
#include <vector>

#include "Database.hpp"

class ApiServer {
public:
    explicit ApiServer(Database& database, std::string dataDirectory = "data");

    void start(unsigned short port = 18080);
    std::vector<std::string> registeredRoutes() const;
    std::string handleTables() const;
    std::string handleTableSummary(const std::string& tableName) const;
    std::string handleTableRows(const std::string& tableName) const;
    std::string handleCreateTable(const std::string& requestBody);
    std::string handleUpdateSchema(const std::string& tableName, const std::string& requestBody);
    std::string handleInsertRow(const std::string& tableName, const std::string& requestBody);
    std::string handleUpdateRows(const std::string& tableName, const std::string& requestBody);
    std::string handleDeleteRows(const std::string& tableName, const std::string& requestBody);
    std::string handleDeleteTable(const std::string& tableName);
    std::string handleLogs() const;
    std::string handlePerformance() const;

private:
    Database& database;
    std::string dataDirectory;
    std::vector<std::string> routes;
};

#endif
