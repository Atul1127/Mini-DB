#ifndef AUTH_HPP
#define AUTH_HPP

#include <string>

// Minimal JWT-style bearer authentication for the Mini-DB API.
// This project intentionally keeps token creation self-contained so the core
// database engine remains dependency-light. Replace the signing primitive with
// a vetted JWT library before production use.
class Auth {
public:
    enum class Role { READER, WRITER, ADMIN };

    explicit Auth(std::string secret = "mini-db-development-secret");

    std::string issueToken(const std::string& username, Role role, int ttlSeconds = 3600) const;
    bool authenticateBearer(const std::string& authorizationHeader,
                            std::string& username,
                            Role& role) const;
    static bool hasPermission(Role role, const std::string& action);

private:
    std::string secret;
};

#endif
