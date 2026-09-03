FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake git ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build && cmake --build build --target mini-db-api -j2

FROM ubuntu:24.04
WORKDIR /app
COPY --from=build /src/build/mini-db-api /app/mini-db-api
RUN mkdir -p /app/data /app/logs
EXPOSE 18080
CMD ["/app/mini-db-api", "18080"]
