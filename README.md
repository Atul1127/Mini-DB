# Mini DB Engine (C++)

## Overview

Built a modular, object-oriented C++ mini database engine with a SQL-like CLI and engine-backed API handlers for table CRUD, row operations, schema updates, persistence, logging, and performance inspection.

Mini DB Engine includes:
- a custom in-memory engine (`Database`, `Table`, `Row`, `Column`)
- persistent storage (`.schema` + `.data` files)
- an interactive SQL-like CLI
- live Crow API routes (engine-backed JSON responses)
- microsecond-level logging and performance summaries

## Current Status

Implemented and working:
- Core engine and persistence
- CLI query execution (`CREATE`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`, `ALTER`, `SHOW`, `DESCRIBE`, `DROP`)
- Live Crow API server for table CRUD, row CRUD, schema updates, logs, and performance
- API smoke test with assertion summary (`API-SMOKE: PASS/FAIL`)
- Persistent logs in `logs/mini-db.log`

Important:
- The live Crow API server runs through `mini-db-api`; `api-smoke` remains the fast handler-level regression test.

## Architecture

```text
CLI Input
  -> QueryParser
  -> Database Engine (Database/Table/Row/Column)
  -> Persistence (data/*.schema, data/*.data)
  -> Logger (logs/mini-db.log)

HTTP API Request
  -> Crow route
  -> ApiServer handler
  -> Database Engine (same core classes as CLI)
  -> Persistence / Logging
  -> JSON response
```

### Core Components

- `Column`: column name + type (`INT`, `FLOAT`, `STRING`)
- `Row`: ordered values (`std::variant<int, float, std::string>`)
- `Table`: schema + rows + type validation + row/schema mutation
- `Database`: multi-table manager + disk load/save
- `QueryParser`: SQL-like command parsing for CLI
- `Logger`: timestamped info/error/perf logs (microsecond precision)
- `ApiServer`: Crow-backed JSON routes and handler methods backed by engine classes

## Features

### 1. SQL-like CLI

Supported command families:
- `CREATE TABLE ...`
- `INSERT INTO ... VALUES (...)`
- `SELECT * FROM ...`
- `UPDATE ... SET ... WHERE ...`
- `DELETE FROM ... WHERE ...`
- `ALTER TABLE ... ADD COLUMN ...`
- `ALTER TABLE ... DROP COLUMN ...`
- `SHOW TABLES`
- `DESCRIBE <table>`
- `DROP TABLE <table>`
- `EXIT`

### 2. Live API Surface (Crow, engine-backed)

Implemented handler routes:
- `GET /tables`
- `POST /tables`
- `GET /tables/:name`
- `DELETE /tables/:name`
- `GET /tables/:name/rows`
- `POST /tables/:name/rows`
- `PUT /tables/:name/rows`
- `DELETE /tables/:name/rows`
- `PUT /tables/:name/schema` (`ADD_COLUMN`, `DROP_COLUMN`)
- `GET /logs` (recent log lines)
- `GET /performance` (PERF summary from logs)

### 3. Persistence

- Schema file per table: `data/<table>.schema`
- Data file per table: `data/<table>.data`
- Load on startup
- Save on write operations (create/insert/update/delete/alter/drop)

### 4. Logging + Performance

`Logger` writes entries like:
- `[timestamp] [INFO] ...`
- `[timestamp] [ERROR] ...`
- `[timestamp] [PERF] Execution time: <N> us`

`/performance` parses PERF lines and returns aggregate fields (`count`, `parsed_count`, `min_us`, `max_us`, `avg_us`) with recent entries.

## Project Structure

```text
mini-db/
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
├── data/
├── logs/
├── docs/
│   ├── NEXT_TASK.md
│   └── WORKLOG.md
├── CMakeLists.txt
└── README.md
```

## Build and Run

### Build

```bash
cmake -S . -B build
cmake --build build
```

### Run CLI

```bash
./build/mini-db
```

### Run Live API Server

```bash
./build/mini-db-api 18080
```

Then call endpoints such as:

```bash
curl http://127.0.0.1:18080/tables
```

### Run API Smoke Test

```bash
./build/api-smoke
```

This runs the API handlers directly and ends with:
- `API-SMOKE: PASS (N assertions)` or
- `API-SMOKE: FAIL (N failures)`

## Example CLI Session

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

## Data Format Details

### `<table>.schema`
Each line:
```text
<column_name> <TYPE>
```

Example:
```text
id INT
name STRING
age INT
```

### `<table>.data`
CSV-like line per row, with quoted string support:
```text
1,"Naitik",21
2,"Aman",22
```

## Crow Integration Note

CMake uses `third_party/crow/crow.h` if it exists. Otherwise, `MINI_DB_FETCH_CROW` fetches Crow and standalone ASIO with `FetchContent`. The live API entrypoint is `mini-db-api`, and route logic is implemented in `ApiServer`.

## Development Workflow

- Active micro-task: `docs/NEXT_TASK.md`
- Historical implementation log: `docs/WORKLOG.md`
- Resume/interview learning guide: `docs/LEARNING_PLAN.md`

This keeps changes incremental and easy to review phase-by-phase.
