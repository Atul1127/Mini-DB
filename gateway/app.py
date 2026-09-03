import json
import os
from functools import wraps

import jwt
import redis
import requests
from flask import Flask, jsonify, request
from kafka import KafkaProducer

app = Flask(__name__)
SECRET = os.getenv("JWT_SECRET", "mini-db-development-secret-change-me")
DB_URL = os.getenv("DB_URL", "http://mini-db:18080")
CACHE_TTL = int(os.getenv("CACHE_TTL", "30"))

cache = redis.Redis.from_url(os.getenv("REDIS_URL", "redis://redis:6379/0"), decode_responses=True)
producer = KafkaProducer(
    bootstrap_servers=os.getenv("KAFKA_BOOTSTRAP", "kafka:9092"),
    value_serializer=lambda value: json.dumps(value).encode("utf-8"),
    retries=5,
)

ROLES = {"READER": {"read"}, "WRITER": {"read", "write"}, "ADMIN": {"read", "write", "admin"}}


def token_for(username, role):
    return jwt.encode({"sub": username, "role": role}, SECRET, algorithm="HS256")


def auth(required="read"):
    def decorator(fn):
        @wraps(fn)
        def wrapper(*args, **kwargs):
            header = request.headers.get("Authorization", "")
            if not header.startswith("Bearer "):
                return jsonify(error="missing bearer token"), 401
            try:
                claims = jwt.decode(header[7:], SECRET, algorithms=["HS256"])
                role = claims.get("role", "READER")
                if required not in ROLES.get(role, set()):
                    return jsonify(error="forbidden"), 403
                request.claims = claims
            except jwt.PyJWTError:
                return jsonify(error="invalid or expired token"), 401
            return fn(*args, **kwargs)
        return wrapper
    return decorator


def emit(event, payload):
    producer.send("mini-db-events", {"event": event, "payload": payload})
    producer.flush(timeout=2)


def proxy(method, path, **kwargs):
    return requests.request(method, DB_URL + path, timeout=10, **kwargs)


@app.post("/auth/token")
def login():
    body = request.get_json(silent=True) or {}
    username = body.get("username")
    role = body.get("role", "READER").upper()
    if not username or role not in ROLES:
        return jsonify(error="username and valid role required"), 400
    # Demo project authentication: users are intentionally static.
    return jsonify(access_token=token_for(username, role), token_type="Bearer", role=role)


@app.get("/tables")
@auth("read")
def tables():
    key = "mini-db:tables"
    cached = cache.get(key)
    if cached:
        return cached, 200, {"Content-Type": "application/json", "X-Cache": "HIT"}
    response = proxy("GET", "/tables")
    if response.ok:
        cache.setex(key, CACHE_TTL, response.text)
    return response.text, response.status_code, {"Content-Type": "application/json", "X-Cache": "MISS"}


@app.route("/tables", methods=["POST"])
@auth("write")
def create_table():
    response = proxy("POST", "/tables", json=request.get_json(silent=True))
    if response.ok:
        cache.delete("mini-db:tables")
        emit("TABLE_CREATED", response.json())
    return response.text, response.status_code, {"Content-Type": "application/json"}


@app.route("/tables/<name>/rows", methods=["GET", "POST", "PUT", "DELETE"])
@auth("read")
def rows(name):
    if request.method != "GET" and not ROLES[request.claims.get("role", "READER")].intersection({"write", "admin"}):
        return jsonify(error="forbidden"), 403
    response = proxy(request.method, f"/tables/{name}/rows", json=request.get_json(silent=True)) if request.method != "GET" else proxy("GET", f"/tables/{name}/rows")
    if response.ok and request.method != "GET":
        cache.delete(f"mini-db:rows:{name}")
        emit(f"ROW_{request.method}", {"table": name})
    if response.ok and request.method == "GET":
        return response.text, response.status_code, {"Content-Type": "application/json", "X-Cache": "MISS"}
    return response.text, response.status_code, {"Content-Type": "application/json"}


@app.get("/health")
def health():
    redis_ok = bool(cache.ping())
    return jsonify(service="mini-db-gateway", redis=redis_ok, kafka=producer.bootstrap_connected())


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
