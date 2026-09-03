import os
from datetime import datetime, timedelta, timezone
from functools import wraps

import jwt
import redis
import requests
from flask import Flask, jsonify, request

app = Flask(__name__)
SECRET = os.getenv("JWT_SECRET", "mini-db-development-secret-change-me")
DB_URL = os.getenv("DB_URL", "http://mini-db:18080")
CACHE_TTL = int(os.getenv("CACHE_TTL", "30"))
DEMO_PASSWORD = os.getenv("DEMO_PASSWORD", "demo123")
cache = redis.Redis.from_url(os.getenv("REDIS_URL", "redis://redis:6379/0"), decode_responses=True)

ROLES = {"READER": {"read"}, "WRITER": {"read", "write"}, "ADMIN": {"read", "write", "admin"}}
USERS = {"atul": "ADMIN", "reader": "READER", "writer": "WRITER"}


def token_for(username, role):
    now = datetime.now(timezone.utc)
    return jwt.encode(
        {"sub": username, "role": role, "iat": now, "exp": now + timedelta(hours=2)},
        SECRET,
        algorithm="HS256",
    )


def auth(required="read"):
    def decorator(fn):
        @wraps(fn)
        def wrapper(*args, **kwargs):
            header = request.headers.get("Authorization", "")
            if not header.startswith("Bearer "):
                return jsonify(error="missing bearer token"), 401
            try:
                claims = jwt.decode(header[7:], SECRET, algorithms=["HS256"])
                role = claims.get("role")
                if required not in ROLES.get(role, set()):
                    return jsonify(error="forbidden"), 403
                request.claims = claims
            except jwt.PyJWTError:
                return jsonify(error="invalid or expired token"), 401
            return fn(*args, **kwargs)
        return wrapper
    return decorator


def proxy(method, path, **kwargs):
    return requests.request(method, DB_URL + path, timeout=10, **kwargs)


def cache_get(key):
    try:
        return cache.get(key)
    except redis.RedisError:
        return None


def cache_set(key, value):
    try:
        cache.setex(key, CACHE_TTL, value)
    except redis.RedisError:
        pass


def cache_delete(key):
    try:
        cache.delete(key)
    except redis.RedisError:
        pass


@app.post("/auth/token")
def login():
    body = request.get_json(silent=True) or {}
    username = body.get("username", "")
    password = body.get("password", "")
    role = USERS.get(username)
    if not role or password != DEMO_PASSWORD:
        return jsonify(error="invalid credentials"), 401
    return jsonify(
        access_token=token_for(username, role),
        token_type="Bearer",
        role=role,
        expires_in=7200,
    )


@app.get("/tables")
@auth("read")
def tables():
    key = "mini-db:tables"
    cached = cache_get(key)
    if cached:
        return cached, 200, {"Content-Type": "application/json", "X-Cache": "HIT"}
    response = proxy("GET", "/tables")
    if response.ok:
        cache_set(key, response.text)
    return response.text, response.status_code, {"Content-Type": "application/json", "X-Cache": "MISS"}


@app.post("/tables")
@auth("write")
def create_table():
    response = proxy("POST", "/tables", json=request.get_json(silent=True))
    if response.ok:
        cache_delete("mini-db:tables")
    return response.text, response.status_code, {"Content-Type": "application/json"}


@app.route("/tables/<name>/rows", methods=["GET", "POST", "PUT", "DELETE"])
@auth("read")
def rows(name):
    role = request.claims.get("role")
    if request.method != "GET" and "write" not in ROLES.get(role, set()):
        return jsonify(error="forbidden"), 403
    key = f"mini-db:rows:{name}"
    if request.method == "GET":
        cached = cache_get(key)
        if cached:
            return cached, 200, {"Content-Type": "application/json", "X-Cache": "HIT"}
        response = proxy("GET", f"/tables/{name}/rows")
        if response.ok:
            cache_set(key, response.text)
        return response.text, response.status_code, {"Content-Type": "application/json", "X-Cache": "MISS"}
    response = proxy(request.method, f"/tables/{name}/rows", json=request.get_json(silent=True))
    if response.ok:
        cache_delete(key)
    return response.text, response.status_code, {"Content-Type": "application/json"}


@app.get("/health")
def health():
    redis_ok = True
    try:
        cache.ping()
    except redis.RedisError:
        redis_ok = False
    db_ok = True
    try:
        db_ok = proxy("GET", "/tables").ok
    except requests.RequestException:
        db_ok = False
    healthy = redis_ok and db_ok
    return jsonify(service="mini-db-gateway", redis=redis_ok, database=db_ok), (200 if healthy else 503)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
