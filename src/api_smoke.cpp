#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ApiServer.hpp"
#include "Database.hpp"

int main() {
    std::filesystem::remove_all("data/api-smoke");
    std::filesystem::create_directories("logs");

    {
        std::ofstream logOut("logs/mini-db.log", std::ios::app);
        logOut << "[2026-06-21 10:00:00.000001] [INFO] api-smoke marker start\n";
        logOut << "[2026-06-21 10:00:00.000050] [PERF] Execution time: 50 us\n";
    }

    Database database;
    ApiServer apiServer(database, "data/api-smoke");

    const std::string createUsersBody =
        R"({"name":"users","columns":[{"name":"id","type":"INT"},{"name":"name","type":"STRING"}]})";
    const std::string createMalformedBody = R"({"name":"broken","columns":[{"name":"id","type":"INT"}])";
    const std::string insertOkBody = R"({"values":[1,"Naitik"]})";
    const std::string insertMalformedArrayBody = R"({"values":[1,"oops"})";
    const std::string insertTypeMismatchBody = R"({"values":["bad","Naitik"]})";
    const std::string insertCountMismatchBody = R"({"values":[1]})";
    const std::string updateOkBody = R"({"set":{"column":"name","value":"Neo"},"where":{"column":"id","value":1}})";
    const std::string updateMalformedObjectBody = R"({"set":{"column":"name","value":"Neo"},"where":{"column":"id","value":1)";
    const std::string updateZeroMatchBody = R"({"set":{"column":"name","value":"Nobody"},"where":{"column":"id","value":99}})";
    const std::string updateMissingColumnBody = R"({"set":{"column":"missing","value":"x"},"where":{"column":"id","value":1}})";
    const std::string updateTypeMismatchBody = R"({"set":{"column":"id","value":"bad"},"where":{"column":"id","value":1}})";
    const std::string deleteOkBody = R"({"where":{"column":"id","value":1}})";
    const std::string deleteZeroMatchBody = R"({"where":{"column":"id","value":99}})";
    const std::string deleteMissingColumnBody = R"({"where":{"column":"missing","value":1}})";
    const std::string deleteTypeMismatchBody = R"({"where":{"column":"id","value":"bad"}})";
    const std::string addColumnOkBody = R"({"action":"ADD_COLUMN","column":{"name":"email","type":"STRING"}})";
    const std::string addColumnDuplicateBody = R"({"action":"ADD_COLUMN","column":{"name":"email","type":"STRING"}})";
    const std::string addColumnInvalidTypeBody = R"({"action":"ADD_COLUMN","column":{"name":"score","type":"BOOL"}})";
    const std::string dropColumnOkBody = R"({"action":"DROP_COLUMN","column":{"name":"email"}})";
    const std::string dropColumnMissingBody = R"({"action":"DROP_COLUMN","column":{"name":"unknown"}})";
    const std::string singleTableBody = R"({"name":"single","columns":[{"name":"id","type":"INT"}]})";
    const std::string dropLastColumnBody = R"({"action":"DROP_COLUMN","column":{"name":"id"}})";

    std::vector<std::string> assertionFailures;
    auto expectContains = [&](const std::string& label, const std::string& response, const std::string& token) {
        if (response.find(token) == std::string::npos) {
            assertionFailures.push_back(label + " missing token: " + token);
        }
    };
    auto printResponse = [](const std::string& response) {
        std::cout << response << '\n';
    };

    const std::string createUsersResp = apiServer.handleCreateTable(createUsersBody);
    const std::string createMalformedResp = apiServer.handleCreateTable(createMalformedBody);
    const std::string createDuplicateResp = apiServer.handleCreateTable(createUsersBody);
    const std::string listTablesResp = apiServer.handleTables();
    const std::string insertOkResp = apiServer.handleInsertRow("users", insertOkBody);
    const std::string insertMalformedResp = apiServer.handleInsertRow("users", insertMalformedArrayBody);
    const std::string insertTypeMismatchResp = apiServer.handleInsertRow("users", insertTypeMismatchBody);
    const std::string insertCountMismatchResp = apiServer.handleInsertRow("users", insertCountMismatchBody);
    const std::string insertMissingTableResp = apiServer.handleInsertRow("missing", insertOkBody);
    const std::string updateOkResp = apiServer.handleUpdateRows("users", updateOkBody);
    const std::string updateMalformedResp = apiServer.handleUpdateRows("users", updateMalformedObjectBody);
    const std::string updateZeroMatchResp = apiServer.handleUpdateRows("users", updateZeroMatchBody);
    const std::string updateMissingColumnResp = apiServer.handleUpdateRows("users", updateMissingColumnBody);
    const std::string updateTypeMismatchResp = apiServer.handleUpdateRows("users", updateTypeMismatchBody);
    const std::string updateMissingTableResp = apiServer.handleUpdateRows("missing", updateOkBody);
    const std::string deleteOkResp = apiServer.handleDeleteRows("users", deleteOkBody);
    const std::string deleteZeroMatchResp = apiServer.handleDeleteRows("users", deleteZeroMatchBody);
    const std::string deleteMissingColumnResp = apiServer.handleDeleteRows("users", deleteMissingColumnBody);
    const std::string deleteTypeMismatchResp = apiServer.handleDeleteRows("users", deleteTypeMismatchBody);
    const std::string deleteMissingTableResp = apiServer.handleDeleteRows("missing", deleteOkBody);
    const std::string insertAmanResp = apiServer.handleInsertRow("users", R"({"values":[2,"Aman"]})");
    const std::string addColumnResp = apiServer.handleUpdateSchema("users", addColumnOkBody);
    const std::string tableRowsWithEmailResp = apiServer.handleTableRows("users");
    const std::string addColumnDuplicateResp = apiServer.handleUpdateSchema("users", addColumnDuplicateBody);
    const std::string addColumnInvalidTypeResp = apiServer.handleUpdateSchema("users", addColumnInvalidTypeBody);
    const std::string dropColumnMissingResp = apiServer.handleUpdateSchema("users", dropColumnMissingBody);
    const std::string dropColumnResp = apiServer.handleUpdateSchema("users", dropColumnOkBody);
    const std::string tableRowsAfterDropResp = apiServer.handleTableRows("users");
    const std::string updateSchemaMissingTableResp = apiServer.handleUpdateSchema("missing", addColumnOkBody);
    const std::string createSingleResp = apiServer.handleCreateTable(singleTableBody);
    const std::string dropLastColumnResp = apiServer.handleUpdateSchema("single", dropLastColumnBody);
    const std::string tableRowsUsersResp = apiServer.handleTableRows("users");
    const std::string tableRowsMissingResp = apiServer.handleTableRows("missing");
    const std::string tableSummaryUsersResp = apiServer.handleTableSummary("users");
    const std::string tableSummaryMissingResp = apiServer.handleTableSummary("missing");
    const std::string deleteUsersResp = apiServer.handleDeleteTable("users");
    const std::string deleteSingleResp = apiServer.handleDeleteTable("single");
    const std::string deleteUsersAgainResp = apiServer.handleDeleteTable("users");
    const std::string finalListTablesResp = apiServer.handleTables();
    const std::string logsResp = apiServer.handleLogs();
    const std::string performanceResp = apiServer.handlePerformance();

    printResponse(createUsersResp);
    printResponse(createMalformedResp);
    printResponse(createDuplicateResp);
    printResponse(listTablesResp);
    printResponse(insertOkResp);
    printResponse(insertMalformedResp);
    printResponse(insertTypeMismatchResp);
    printResponse(insertCountMismatchResp);
    printResponse(insertMissingTableResp);
    printResponse(updateOkResp);
    printResponse(updateMalformedResp);
    printResponse(updateZeroMatchResp);
    printResponse(updateMissingColumnResp);
    printResponse(updateTypeMismatchResp);
    printResponse(updateMissingTableResp);
    printResponse(deleteOkResp);
    printResponse(deleteZeroMatchResp);
    printResponse(deleteMissingColumnResp);
    printResponse(deleteTypeMismatchResp);
    printResponse(deleteMissingTableResp);
    printResponse(insertAmanResp);
    printResponse(addColumnResp);
    printResponse(tableRowsWithEmailResp);
    printResponse(addColumnDuplicateResp);
    printResponse(addColumnInvalidTypeResp);
    printResponse(dropColumnMissingResp);
    printResponse(dropColumnResp);
    printResponse(tableRowsAfterDropResp);
    printResponse(updateSchemaMissingTableResp);
    printResponse(createSingleResp);
    printResponse(dropLastColumnResp);
    printResponse(tableRowsUsersResp);
    printResponse(tableRowsMissingResp);
    printResponse(tableSummaryUsersResp);
    printResponse(tableSummaryMissingResp);
    printResponse(deleteUsersResp);
    printResponse(deleteSingleResp);
    printResponse(deleteUsersAgainResp);
    printResponse(finalListTablesResp);
    printResponse(logsResp);
    printResponse(performanceResp);

    expectContains("create users", createUsersResp, "\"success\":true");
    expectContains("create users", createUsersResp, "\"route\":\"/tables\"");
    expectContains("insert ok", insertOkResp, "\"success\":true");
    expectContains("update ok", updateOkResp, "Updated 1 row(s)");
    expectContains("delete ok", deleteOkResp, "Deleted 1 row(s)");
    expectContains("malformed create", createMalformedResp, "\"success\":false");
    expectContains("malformed insert", insertMalformedResp, "\"success\":false");
    expectContains("malformed update", updateMalformedResp, "\"success\":false");
    expectContains("logs route", logsResp, "\"route\":\"/logs\"");
    expectContains("logs route", logsResp, "\"success\":true");
    expectContains("performance route", performanceResp, "\"route\":\"/performance\"");
    expectContains("performance route", performanceResp, "\"success\":true");

    if (assertionFailures.empty()) {
        std::cout << "API-SMOKE: PASS (12 assertions)\n";
    } else {
        std::cout << "API-SMOKE: FAIL (" << assertionFailures.size() << " failures)\n";
        for (const auto& failure : assertionFailures) {
            std::cout << " - " << failure << '\n';
        }
    }

    return 0;
}
