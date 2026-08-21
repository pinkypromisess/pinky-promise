# syntax=docker/dockerfile:1

########################################
# Stage 1: build Drogon (pinned) and this backend
########################################
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    libssl-dev \
    libpq-dev \
    libjsoncpp-dev \
    uuid-dev \
    zlib1g-dev \
    libc-ares-dev \
    libbrotli-dev \
    && rm -rf /var/lib/apt/lists/*

# Debian doesn't package Drogon, so build it from source, pinned to the
# version this backend was developed against, with Postgres support on
# (the default bottled build lacks it) and the other DB backends off.
ARG DROGON_VERSION=v1.9.13
RUN git clone --branch ${DROGON_VERSION} --depth 1 \
        https://github.com/drogonframework/drogon.git /tmp/drogon \
    && cd /tmp/drogon \
    && git submodule update --init --depth 1 \
    && mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_POSTGRESQL=ON -DBUILD_MYSQL=OFF -DBUILD_SQLITE=OFF -DBUILD_REDIS=OFF \
    && cmake --build . -j"$(nproc)" \
    && cmake --install . \
    && rm -rf /tmp/drogon

WORKDIR /app
COPY CMakeLists.txt ./
COPY src ./src
COPY config ./config

RUN mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && cmake --build . -j"$(nproc)" --target pinky_promise_backend

########################################
# Stage 2: minimal runtime image
########################################
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libpq5 \
    libjsoncpp25 \
    libc-ares2 \
    libbrotli1 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home appuser

WORKDIR /app
COPY --from=build /app/build/pinky_promise_backend ./pinky_promise_backend
COPY --from=build /app/config ./config

# Cloud Run sets PORT; src/main.cc reads it directly and listens on it.
ENV PORT=8080
EXPOSE 8080

USER appuser
ENTRYPOINT ["./pinky_promise_backend"]
