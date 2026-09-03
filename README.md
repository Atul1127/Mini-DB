# Mini DB Engine (C++)

A modular C++ mini database engine with a SQL-like CLI, a Crow HTTP API, file persistence, logging, performance inspection, and a small Python gateway demonstrating JWT/RBAC and Redis caching.

## Highlights

- **C++17 OOP engine** — `Database`, `Table`, `Row`, and `Column`
- **SQL-like CLI** — create, insert, select, update, delete, alter, describe, show, drop
- **Crow REST API** — engine-backed table and row operations
- **Persistence** — `.schema` and `.data` files loaded on startup and saved after writes
- **Logging + performance** — INFO/ERROR/PERF entries with microsecond timing
- **JWT + RBAC gateway** — `READER`, `WRITER`, and `ADMIN`
- **Redis cache-aside** — table and row reads with TTL and write invalidation
- **Docker Compose** — runs the C++ database, Python gateway, and Redis together
- **API smoke test** — handler-level regression coverage with assertion output

> **Scope:** This is a learning/portfolio project, not a production database or production authentication system.

## Architecture

```text
                         +----------------------+
                         |      Client/curl      |
                         +----------+-----------+
                                    |
                                    v
                         +----------------------+
                         | Flask API Gateway     |
                         | :8080                 |
                         | JWT + RBAC            |
                         +----+-------------+----+
                              |             |
                       cache hit            | cache miss / write
                              |             v
                              |     +----------------+
                              +---->| Redis :6379    |
                                    +----------------+
                                            |
                                            | proxy
                                            v
                                    +----------------+
                                    | C++ Mini-DB    |
                                    | Crow :18080    |
                                    +-------+--------+
                                            |
                                  +---------+---------+
                                  |                   |
                                  v                   v
                           File persistence      Logger/PERF
                           data/*.schema         logs/mini-db.log
                           data/*.data

CLI path:
CLI -> QueryParser -> Database -> Table/Row/Column -> Persistence/Logger
```

### Gateway responsibilities

The gateway is intentionally small and demonstrates three practical SDE concepts:

1. **Authentication:** `/auth/token` issues short-lived JWTs.
2. **Authorization:** `READER` can read; `WRITER` can read/write; `ADMIN` has read/write/admin permissions.
3. **Caching:** successful reads use Redis; successful writes invalidate the relevant cache entry.

The native C++ API stays internal to the Compose network. Only the gateway publishes port `8080` to the host.

## Core Components

| Component | Responsibility |
|---|---|
| `Column` | Column name and `INT` / `FLOAT` / `STRING` type |
| `Row` | Ordered row values using `std::variant` |
| `Table` | Schema, rows, type validation, row/schema mutations |
| `Database` | Multi-table management and disk persistence |
| `QueryParser` | SQL-like CLI command parsing and execution |
| `Logger` | INFO, ERROR, and PERF logging |
| `ApiServer` | Crow routes and engine-backed JSON handlers |
| `gateway/app.py` | JWT, RBAC, Redis cache-aside, and API proxy |

## SQL-like CLI

Supported commands include:

```text
CREATE TABLE
INSERT INTO ... VALUES
SELECT * FROM
UPDATE ... SET ... WHERE
DELETE FROM ... WHERE
ALTER TABLE ... ADD COLUMN
ALTER TABLE ... DROP COLUMN
SHOW TABLES
DESCRIBE <table>
DROP TABLE <table>
EXIT
```

Example:

```sql
CREATE TABLE users (id INT, name STRING, age INT);
INSERT INTO users VALUES (1, "Naitik", 21);
SELECT * FROM users;
UPDATE users SET age = 22 WHERE id = 1;
ALTER TABLE users ADD COLUMN email STRING;
DESCRIBE users;
SHOW TABLES;
EXIT
```

## HTTP API

### Native C++ API

When running the binary directly, the Crow API listens on port `18080`.

| Method | Route | Purpose |
|---|---|---|
| GET | `/tables` | List tables |
| POST | `/tables` | Create table |
| GET | `/tables/:name` | Table summary |
| DELETE | `/tables/:name` | Delete table |
| GET | `/tables/:name/rows` | Read rows |
| POST | `/tables/:name/rows` | Insert row |
| PUT | `/tables/:name/rows` | Update rows |
| DELETE | `/tables/:name/rows` | Delete rows |
| PUT | `/tables/:name/schema` | Add/drop column |
| GET | `/logs` | Recent logs |
| GET | `/performance` | Performance summary |

### Gateway API

The Dockerized gateway exposes:

| Method | Route | Access |
|---|---|---|
| POST | `/auth/token` | Public login |
| GET | `/health` | Public health check |
| GET | `/tables` | READER+ |
| POST | `/tables` | WRITER+ |
| GET | `/tables/:name/rows` | READER+ |
| POST | `/tables/:name/rows` | WRITER+ |
| PUT | `/tables/:name/rows` | WRITER+ |
| DELETE | `/tables/:name/rows` | WRITER+ |

The gateway currently exposes the core table/row workflow rather than every native C++ route. This keeps the integration layer focused and easy to understand.

## Docker Compose

### Prerequisites

- Docker Desktop with Docker Compose
- Git

### Start everything

From the repository root:

```bash
git clone https://github.com/Atul1127/Mini-DB.git
cd Mini-DB
docker compose up --build
```

The first build may take a few minutes because the C++ image builds the project and CMake can fetch Crow/ASIO when needed.

Services:

```text
mini-db-gateway  -> http://localhost:8080
mini-db          -> internal :18080
redis            -> internal :6379
```

### Rebuild after Dockerfile changes

```bash
docker compose down
docker compose build --no-cache mini-db
docker compose up
```

### Stop services

```bash
docker compose down
```

Named Docker volumes keep database data and logs between container recreations.

## Gateway Configuration

Optional environment variables:

```bash
JWT_SECRET=replace-with-a-development-secret
DEMO_PASSWORD=demo123
CACHE_TTL=30
```

The Compose file provides development defaults. Do not use the defaults for a real deployment.

### Demo users

```text
atul   -> ADMIN
writer -> WRITER
reader -> READER
```

All demo users use `DEMO_PASSWORD` (default: `demo123`).

> The gateway intentionally uses an in-memory demo user map. Production authentication should use an identity provider or a proper user store, password hashing, secure secret management, and stronger operational controls.

## Quick Gateway Demo

### 1. Health

```bash
curl http://localhost:8080/health
```

Expected shape:

```json
{"database":true,"redis":true,"service":"mini-db-gateway"}
```

### 2. Get an ADMIN token

```bash
curl -X POST http://localhost:8080/auth/token \
  -H "Content-Type: application/json" \
  -d '{"username":"atul","password":"demo123"}'
```

Copy the returned `access_token` and set it in your shell:

```bash
TOKEN="YOUR_ACTUAL_JWT"
```

### 3. Verify Redis caching

```bash
curl -i http://localhost:8080/tables \
  -H "Authorization: Bearer $TOKEN"
```

The first successful read should return:

```text
X-Cache: MISS
```

Repeat the request:

```bash
curl -i http://localhost:8080/tables \
  -H "Authorization: Bearer $TOKEN"
```

A cached response should return:

```text
X-Cache: HIT
```

### 4. Verify RBAC

Get a reader token:

```bash
curl -X POST http://localhost:8080/auth/token \
  -H "Content-Type: application/json" \
  -d '{"username":"reader","password":"demo123"}'
```

Use the actual token:

```bash
READER_TOKEN="YOUR_ACTUAL_READER_JWT"
```

Reader access should succeed:

```bash
curl -i http://localhost:8080/tables \
  -H "Authorization: Bearer $READER_TOKEN"
```

Reader writes should be rejected:

```bash
curl -i -X POST http://localhost:8080/tables \
  -H "Authorization: Bearer $READER_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"name":"test","columns":[{"name":"id","type":"INT"}]}'
```

Expected:

```text
HTTP/1.1 403 FORBIDDEN
```

### 5. Verify cache invalidation

Create a table with the ADMIN token:

```bash
curl -i -X POST http://localhost:8080/tables \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"name":"test","columns":[{"name":"id","type":"INT"}]}'
```

Then read `/tables` again. A successful write invalidates the table-list cache, so the next read should be `X-Cache: MISS`; the following read should be `X-Cache: HIT`.

## Local C++ Build

### Build

```bash
cmake -S . -B build
cmake --build build
```

### Run the CLI

```bash
./build/mini-db
```

### Run the native API

```bash
./build/mini-db-api 18080
```

Then:

```bash
curl http://127.0.0.1:18080/tables
```

### Run the API smoke test

```bash
./build/api-smoke
```

Expected final line:

```text
API-SMOKE: PASS (12 assertions)
```

The smoke test exercises successful and malformed table/row/schema operations plus logs and performance handlers.

## Persistence

Each table uses two files:

```text
data/<table>.schema
data/<table>.data
```

The application loads persisted data at startup and saves after successful write operations.

Example schema:

```text
id INT
name STRING
age INT
```

Example data:

```text
1,"Naitik",21
2,"Aman",22
```

## Logging and Performance

Logs are written to:

```text
logs/mini-db.log
```

Entries include:

```text
[timestamp] [INFO] ...
[timestamp] [ERROR] ...
[timestamp] [PERF] Execution time: <N> us
```

The native `/performance` route parses recent PERF entries and reports aggregate fields such as `count`, `parsed_count`, `min_us`, `max_us`, and `avg_us`.

## Project Structure

```text
Mini-DB/
├── include/
│   ├── ApiServer.hpp
│   ├── Column.hpp
│   ├── Database.hpp
│   ├── Logger.hpp
│   ├── QueryParser.hpp
│   ├── Row.hpp
│   └── Table.hpp
├── src/
│   ├── ApiServer.cpp
│   ├── Column.cpp
│   ├── Database.cpp
│   ├── Logger.cpp
│   ├── QueryParser.cpp
│   ├── Row.cpp
│   ├── Table.cpp
│   ├── api_main.cpp
│   ├── api_smoke.cpp
│   └── main.cpp
├── gateway/
│   ├── app.py
│   ├── Dockerfile
│   ├── README.md
│   └── requirements.txt
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── .gitignore
└── README.md
```

## Crow Dependency

If `third_party/crow/crow.h` exists, CMake uses it directly. Otherwise, `MINI_DB_FETCH_CROW` fetches Crow and standalone ASIO through CMake `FetchContent`.

## Design Decisions

- **Redis:** kept because it demonstrates a real cache-aside pattern with explicit invalidation.
- **JWT/RBAC:** kept because authentication and authorization are useful backend fundamentals.
- **Docker Compose:** kept because the project now contains multiple cooperating services.
- **Kafka:** intentionally not included. There is no genuine asynchronous workflow in this project, so adding Kafka would increase complexity without demonstrating a meaningful use case.
- **Kubernetes/CI/CD/service discovery:** intentionally out of scope for this fresher portfolio project.

## License

No license is currently specified for this learning project.
