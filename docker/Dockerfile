# ==============================================================================
# STAGE 1: High-Performance Multi-Core Builder
# ==============================================================================
FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    ccache \
    mold \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    ca-certificates \
    libopus-dev \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone --depth=1 https://github.com/microsoft/vcpkg.git $VCPKG_ROOT \
    && $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /app

# Cache dependency layer
COPY vcpkg.json vcpkg-configuration.json* ./
RUN $VCPKG_ROOT/vcpkg install --triplet x64-linux-release

# Copy project source tree
COPY . .

# Configure and compile with Ninja and Mold
RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=x64-linux-release \
    && cmake --build build --target anonx_bot --parallel $(nproc)

# ==============================================================================
# STAGE 2: Production Minimal Runtime (<180MB)
# ==============================================================================
FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONUNBUFFERED=1

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    python3 \
    python3-minimal \
    ffmpeg \
    libssl3 \
    libopus0 \
    tini \
    && rm -rf /var/lib/apt/lists/*

# Install standalone yt-dlp binary
RUN curl -sSL https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp -o /usr/local/bin/yt-dlp \
    && chmod a+rx /usr/local/bin/yt-dlp

# Create dedicated non-root application user
RUN useradd -m -u 1000 -s /bin/bash anonx \
    && mkdir -p /app/data /app/downloads /app/sessions \
    && chown -R anonx:anonx /app

WORKDIR /app

# Copy stripped binary from builder
COPY --from=builder /app/build/anonx_bot /usr/local/bin/anonx_bot
COPY docker/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /usr/local/bin/anonx_bot /app/entrypoint.sh

USER anonx

VOLUME ["/app/data", "/app/sessions", "/app/downloads"]

ENTRYPOINT ["/usr/bin/tini", "--", "/app/entrypoint.sh"]
CMD ["anonx_bot"]
