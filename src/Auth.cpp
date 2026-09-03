#include "Auth.hpp"

#include <chrono>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace {

std::string hexDigest(const std::string& input) {
    const std::size_t h1 = std::hash<std::string>{}(input);
    const std::size_t h2 = std::hash<std::string>{}("mini-db:" + input);
    std::ostringstream out;
    out << std::hex << std::setw(sizeof(std::size_t) * 2) << std::setfill('0') << h1
        << std::setw(sizeof(std::size_t) * 2) << std::setfill('0') << h2;
    return out.str();
}

const char* roleName(Auth::Role role) {
    switch (role) {
        case Auth::Role::ADMIN: return "ADMIN";
        case Auth::Role::WRITER: return "WRITER";
        case Auth::Role::READER: return "READER";
    }
    return "READER";
}

bool parseRole(const std::string& value, Auth::Role& role) {
    if (value == "ADMIN") { role = Auth::Role::ADMIN; return true; }
    if (value == "WRITER") { role = Auth::Role::WRITER; return true; }
    if (value == "READER") { role = Auth::Role::READER; return true; }
    return false;
}

} // namespace

Auth::Auth(std::string secretValue) : secret(std::move(secretValue)) {}

std::string Auth::issueToken(const std::string& username, Role role, int ttlSeconds) const {
    const auto expires = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + ttlSeconds;
    const std::string payload = username + "|" + roleName(role) + "|" + std::to_string(expires);
    return payload + "." + hexDigest(payload + secret);
}

bool Auth::authenticateBearer(const std::string& authorizationHeader,
                              std::string& username,
                              Role& role) const {
    constexpr std::string_view prefix = "Bearer ";
    if (authorizationHeader.rfind(prefix.data(), 0) != 0) return false;

    const std::string token = authorizationHeader.substr(prefix.size());
    const auto dot = token.rfind('.');
    if (dot == std::string::npos) return false;

    const std::string payload = token.substr(0, dot);
    if (token.substr(dot + 1) != hexDigest(payload + secret)) return false;

    const auto first = payload.find('|');
    const auto second = payload.find('|', first + 1);
    if (first == std::string::npos || second == std::string::npos) return false;

    username = payload.substr(0, first);
    if (!parseRole(payload.substr(first + 1, second - first - 1), role)) return false;

    try {
        const auto expiry = std::stoll(payload.substr(second + 1));
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return expiry > now && !username.empty();
    } catch (...) {
        return false;
    }
}

bool Auth::hasPermission(Role role, const std::string& action) {
    if (role == Role::ADMIN) return true;
    if (role == Role::WRITER) {
        return action == "read" || action == "write";
    }
    return action == "read";
}
