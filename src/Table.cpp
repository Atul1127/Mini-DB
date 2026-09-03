#include "Table.hpp"

#include <algorithm>
#include <variant>
#include <utility>

Table::Table(std::string name, std::vector<Column> columns)
    : name(std::move(name)), columns(std::move(columns)) {}

const std::string& Table::getName() const {
    return name;
}

const std::vector<Column>& Table::getColumns() const {
    return columns;
}

const std::vector<Row>& Table::getRows() const {
    return rows;
}

bool Table::insertRow(const Row& row, std::string& errorMessage) {
    const auto& values = row.getValues();
    if (values.size() != columns.size()) {
        errorMessage = "Column count mismatch. Expected " + std::to_string(columns.size()) +
                       ", got " + std::to_string(values.size()) + ".";
        return false;
    }

    for (std::size_t i = 0; i < columns.size(); ++i) {
        const DataType expectedType = columns[i].getType();
        const Value& value = values[i];
        bool matchesType = false;

        switch (expectedType) {
            case DataType::INT:
                matchesType = std::holds_alternative<int>(value);
                break;
            case DataType::FLOAT:
                matchesType = std::holds_alternative<float>(value);
                break;
            case DataType::STRING:
                matchesType = std::holds_alternative<std::string>(value);
                break;
        }

        if (!matchesType) {
            errorMessage = "Type mismatch for column '" + columns[i].getName() +
                           "'. Expected " + columns[i].getTypeAsString() + ".";
            return false;
        }
    }

    rows.push_back(row);
    errorMessage.clear();
    return true;
}

bool Table::deleteWhereEquals(const std::string& columnName,
                              const Value& value,
                              std::size_t& deletedCount,
                              std::string& errorMessage) {
    deletedCount = 0;

    std::size_t columnIndex = columns.size();
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].getName() == columnName) {
            columnIndex = i;
            break;
        }
    }

    if (columnIndex == columns.size()) {
        errorMessage = "Column '" + columnName + "' does not exist.";
        return false;
    }

    const DataType expectedType = columns[columnIndex].getType();
    const bool typeMatches =
        (expectedType == DataType::INT && std::holds_alternative<int>(value)) ||
        (expectedType == DataType::FLOAT && std::holds_alternative<float>(value)) ||
        (expectedType == DataType::STRING && std::holds_alternative<std::string>(value));

    if (!typeMatches) {
        errorMessage = "Type mismatch for column '" + columnName +
                       "'. Expected " + columns[columnIndex].getTypeAsString() + ".";
        return false;
    }

    const std::size_t oldSize = rows.size();
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Row& row) {
                   const auto& values = row.getValues();
                   if (columnIndex >= values.size()) {
                       return false;
                   }
                   return values[columnIndex] == value;
               }),
               rows.end());
    deletedCount = oldSize - rows.size();

    errorMessage.clear();
    return true;
}

bool Table::updateWhereEquals(const std::string& targetColumnName,
                              const Value& targetValue,
                              const std::string& matchColumnName,
                              const Value& matchValue,
                              std::size_t& updatedCount,
                              std::string& errorMessage) {
    updatedCount = 0;

    std::size_t targetColumnIndex = columns.size();
    std::size_t matchColumnIndex = columns.size();
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].getName() == targetColumnName) {
            targetColumnIndex = i;
        }
        if (columns[i].getName() == matchColumnName) {
            matchColumnIndex = i;
        }
    }

    if (targetColumnIndex == columns.size()) {
        errorMessage = "Column '" + targetColumnName + "' does not exist.";
        return false;
    }
    if (matchColumnIndex == columns.size()) {
        errorMessage = "Column '" + matchColumnName + "' does not exist.";
        return false;
    }

    const DataType targetExpectedType = columns[targetColumnIndex].getType();
    const bool targetTypeMatches =
        (targetExpectedType == DataType::INT && std::holds_alternative<int>(targetValue)) ||
        (targetExpectedType == DataType::FLOAT && std::holds_alternative<float>(targetValue)) ||
        (targetExpectedType == DataType::STRING && std::holds_alternative<std::string>(targetValue));
    if (!targetTypeMatches) {
        errorMessage = "Type mismatch for column '" + targetColumnName +
                       "'. Expected " + columns[targetColumnIndex].getTypeAsString() + ".";
        return false;
    }

    const DataType matchExpectedType = columns[matchColumnIndex].getType();
    const bool matchTypeMatches =
        (matchExpectedType == DataType::INT && std::holds_alternative<int>(matchValue)) ||
        (matchExpectedType == DataType::FLOAT && std::holds_alternative<float>(matchValue)) ||
        (matchExpectedType == DataType::STRING && std::holds_alternative<std::string>(matchValue));
    if (!matchTypeMatches) {
        errorMessage = "Type mismatch for column '" + matchColumnName +
                       "'. Expected " + columns[matchColumnIndex].getTypeAsString() + ".";
        return false;
    }

    for (Row& row : rows) {
        auto values = row.getValues();
        if (matchColumnIndex >= values.size() || targetColumnIndex >= values.size()) {
            continue;
        }
        if (values[matchColumnIndex] != matchValue) {
            continue;
        }
        values[targetColumnIndex] = targetValue;

        Row updatedRow;
        for (const auto& value : values) {
            updatedRow.addValue(value);
        }
        row = updatedRow;
        ++updatedCount;
    }

    errorMessage.clear();
    return true;
}

bool Table::addColumn(const Column& column, std::string& errorMessage) {
    for (const auto& existingColumn : columns) {
        if (existingColumn.getName() == column.getName()) {
            errorMessage = "Column '" + column.getName() + "' already exists.";
            return false;
        }
    }

    columns.push_back(column);

    Value defaultValue = std::string("");
    switch (column.getType()) {
        case DataType::INT:
            defaultValue = 0;
            break;
        case DataType::FLOAT:
            defaultValue = 0.0f;
            break;
        case DataType::STRING:
            defaultValue = std::string("");
            break;
    }

    for (Row& row : rows) {
        row.addValue(defaultValue);
    }

    errorMessage.clear();
    return true;
}

bool Table::dropColumn(const std::string& columnName, std::string& errorMessage) {
    if (columns.size() <= 1) {
        errorMessage = "Cannot drop the last remaining column.";
        return false;
    }

    std::size_t columnIndex = columns.size();
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].getName() == columnName) {
            columnIndex = i;
            break;
        }
    }

    if (columnIndex == columns.size()) {
        errorMessage = "Column '" + columnName + "' does not exist.";
        return false;
    }

    columns.erase(columns.begin() + columnIndex);

    for (Row& row : rows) {
        const auto oldValues = row.getValues();
        Row rebuiltRow;
        for (std::size_t i = 0; i < oldValues.size(); ++i) {
            if (i == columnIndex) {
                continue;
            }
            rebuiltRow.addValue(oldValues[i]);
        }
        row = rebuiltRow;
    }

    errorMessage.clear();
    return true;
}
