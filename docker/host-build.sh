#!/bin/sh
# Build the host-dev image if missing, then run a command inside it with the repo mounted at /work.
# Usage: docker/host-build.sh <command...>   e.g.  docker/host-build.sh make -C port -f Makefile.linux -j32
# Env: IMAGE (default sh3host:local), GAME_DATA (host path of "Silent Hill 3", mounted read-only at /game)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(dirname "$HERE")
IMAGE=${IMAGE:-sh3host:local}
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  docker build -t "$IMAGE" -f "$HERE/host-dev.Dockerfile" "$HERE"
fi
if [ -n "${GAME_DATA:-}" ]; then
  exec docker run --rm -v "$REPO:/work" -v "$GAME_DATA:/game:ro" -u "$(id -u):$(id -g)" -w /work "$IMAGE" "$@"
fi
exec docker run --rm -v "$REPO:/work" -u "$(id -u):$(id -g)" -w /work "$IMAGE" "$@"
