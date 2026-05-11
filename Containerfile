# ══════════════════════════════════════════════════════════════════════════════
#  SdrScanner — Qt6 C++ SDR band scanner UI
#  Multi-stage: builder → runtime
#
#  Build:
#    podman build -t sdr-scanner:1.0 .
#
#  Run (requires X11 or Wayland forwarding):
#    # X11:
#    podman run --rm -e DISPLAY=$DISPLAY \
#      -v /tmp/.X11-unix:/tmp/.X11-unix:z \
#      --network=host sdr-scanner:1.0
#
#    # Wayland:
#    podman run --rm -e WAYLAND_DISPLAY=$WAYLAND_DISPLAY \
#      -e XDG_RUNTIME_DIR=/run/user/1000 \
#      -v $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY:/run/user/1000/$WAYLAND_DISPLAY:z \
#      --network=host sdr-scanner:1.0
# ══════════════════════════════════════════════════════════════════════════════

FROM ubuntu:24.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake pkg-config git ca-certificates \
    qt6-base-dev \
    libqpid-proton-cpp12-dev \
    libfftw3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt .
COPY src/            src/

RUN cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/install \
        -DFETCHCONTENT_QUIET=OFF \
    && cmake --build build --parallel "$(nproc)" \
    && cmake --install build


# ── Runtime ───────────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libqt6widgets6t64 libqt6core6t64 libqt6gui6t64 libqt6dbus6t64 \
    qt6-qpa-plugins \
    libqpid-proton-cpp12 \
    libfftw3-single3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /install/bin/sdr_scanner /usr/local/bin/sdr_scanner

ENV QT_QPA_PLATFORM=xcb

ENTRYPOINT ["/usr/local/bin/sdr_scanner"]
