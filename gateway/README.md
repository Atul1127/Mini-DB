# Mini-DB API Gateway

The gateway adds the four SDE-focused integration layers around the existing C++ Mini-DB API:

- JWT authentication with `READER`, `WRITER`, and `ADMIN` roles
- Redis cache-aside caching for read-heavy table metadata
- Kafka events on table/row mutations via the `mini-db-events` topic
- A Docker container for the gateway

## Run

From the repository root:

```bash
docker compose up --build
```

The gateway is exposed on `http://localhost:8080` and the native C++ API remains internal on port `18080`.

## Demo token

This portfolio implementation intentionally uses a simple token-issuing endpoint instead of a user database:

```bash
curl -X POST http://localhost:8080/auth/token \
  -H 'Content-Type: application/json' \
  -d '{"username":"atul","role":"ADMIN"}'
```

Use the returned token as `Authorization: Bearer <token>`.

> This is a learning/portfolio implementation. Production authentication should use a proper identity provider, secret management, HTTPS, refresh-token rotation, and audited libraries/configuration.
