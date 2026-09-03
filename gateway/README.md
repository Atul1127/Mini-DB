# Mini-DB API Gateway

The gateway adds three SDE-focused integration layers around the existing C++ Mini-DB API:

- JWT authentication with `READER`, `WRITER`, and `ADMIN` roles
- Redis cache-aside caching for table and row reads
- Docker containerization with Docker Compose

## Run

From the repository root:

```bash
docker compose up --build
```

The gateway is exposed on `http://localhost:8080` and the native C++ API remains internal on port `18080`.

## Demo users

This portfolio implementation uses a small in-memory user list instead of a user database. All demo users use the same password from `DEMO_PASSWORD` (default: `demo123`).

```text
atul   -> ADMIN
writer -> WRITER
reader -> READER
```

Get a token:

```bash
curl -X POST http://localhost:8080/auth/token \
  -H 'Content-Type: application/json' \
  -d '{"username":"atul","password":"demo123"}'
```

Use the returned token as `Authorization: Bearer <token>`.

> This is a learning/portfolio implementation. Production authentication should use a proper identity provider, hashed credentials, and secure secret management.
