#pragma once

#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace iphox::cognitive {

class StateProjection final {
public:
    explicit StateProjection(std::set<std::string> allowedKeys)
        : allowedKeys_(std::move(allowedKeys)) {}

    void Add(std::string key, std::string value) {
        if (!allowedKeys_.contains(key)) {
            throw std::invalid_argument("state key is not allow-listed: " + key);
        }
        values_.insert_or_assign(std::move(key), std::move(value));
    }

    [[nodiscard]] const std::map<std::string, std::string>& Values() const noexcept {
        return values_;
    }

private:
    std::set<std::string> allowedKeys_;
    std::map<std::string, std::string> values_;
};

} // namespace iphox::cognitive
