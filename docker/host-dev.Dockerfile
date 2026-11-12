# Host (x86_64 Linux) development image for the SH3 port: headless/SDL2 harness builds and test runs.
# The Vita image (vita-sdk.Dockerfile) cross-compiles for ARM; this one builds natively so the recompiled
# game can be run and debugged on the workstation. No game files are baked into the image.
FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential gdb make cmake python3 pkg-config ca-certificates \
      libsdl2-dev libgl1-mesa-dev libglu1-mesa-dev xvfb xauth \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
