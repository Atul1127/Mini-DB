cmake_minimum_required(VERSION 3.16)
project(mini_db_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(MINI_DB_FETCH_CROW "Fetch Crow if third_party/crow/crow.h is not present" ON)

set(MINI_DB_ENGINE_SOURCES
    src/Column.cpp
    src/Row.cpp
    src/Table.cpp
    src/Database.cpp
    src/QueryParser.cpp
    src/Logger.cpp
    src/ApiServer.cpp
)

add_executable(mini-db
    src/main.cpp
    ${MINI_DB_ENGINE_SOURCES}
)

target_include_directories(mini-db PRIVATE include)

add_executable(api-smoke
    src/api_smoke.cpp
    ${MINI_DB_ENGINE_SOURCES}
)

target_include_directories(api-smoke PRIVATE include)

add_executable(mini-db-api
    src/api_main.cpp
    ${MINI_DB_ENGINE_SOURCES}
)

target_include_directories(mini-db-api PRIVATE include)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/crow/crow.h")
    target_include_directories(mini-db PRIVATE third_party/crow)
    target_include_directories(api-smoke PRIVATE third_party/crow)
    target_include_directories(mini-db-api PRIVATE third_party/crow)
    target_compile_definitions(mini-db PRIVATE MINI_DB_HAS_CROW)
    target_compile_definitions(api-smoke PRIVATE MINI_DB_HAS_CROW)
    target_compile_definitions(mini-db-api PRIVATE MINI_DB_HAS_CROW)
elseif(MINI_DB_FETCH_CROW)
    FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-30-2
    )
    FetchContent_GetProperties(asio)
    if(NOT asio_POPULATED)
        FetchContent_Populate(asio)
    endif()
    set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include" CACHE PATH "" FORCE)

    FetchContent_Declare(
        Crow
        GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
        GIT_TAG v1.3.3
    )
    set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Crow)

    target_link_libraries(mini-db PRIVATE Crow::Crow)
    target_link_libraries(api-smoke PRIVATE Crow::Crow)
    target_link_libraries(mini-db-api PRIVATE Crow::Crow)
    target_compile_definitions(mini-db PRIVATE MINI_DB_HAS_CROW)
    target_compile_definitions(api-smoke PRIVATE MINI_DB_HAS_CROW)
    target_compile_definitions(mini-db-api PRIVATE MINI_DB_HAS_CROW)
endif()
