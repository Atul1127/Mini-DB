#include "Column.hpp"

#include <utility>

Column::Column(std::string name, DataType type)
    : name(std::move(name)), type(type) {}

const std::string& Column::getName() const {
    return name;
}

DataType Column::getType() const {
    return type;
}

std::string Column::getTypeAsString() const {
    switch (type) {
        case DataType::INT:
            return "INT";
        case DataType::FLOAT:
            return "FLOAT";
        case DataType::STRING:
            return "STRING";
    }
    return "UNKNOWN";
}
