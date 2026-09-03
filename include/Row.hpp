#ifndef ROW_HPP
#define ROW_HPP

#include <string>
#include <variant>
#include <vector>

using Value = std::variant<int, float, std::string>;

class Row {
public:
    void addValue(const Value& value);
    const std::vector<Value>& getValues() const;

private:
    std::vector<Value> values;
};

#endif
