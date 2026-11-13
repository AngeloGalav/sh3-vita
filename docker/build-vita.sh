#!/usr/bin/env bash
# Compile le port Vita dans le conteneur, sans rien installer sur l'hôte.
#
#   docker/build-vita.sh                       # build complet (nécessite port/gen/)
#   docker/build-vita.sh -DSH4_GUEST_MB=200    # options CMake supplémentaires
#   IMAGE=sh3vita-sdk:local CACHE=sh3vita-build docker/build-vita.sh
#
# Le dossier de build CMake vit dans un volume Docker (CACHE) : les rebuilds
# successifs sont incrémentaux, y compris après une modification du dépôt.
# Le VPK est déposé dans build/ à la racine du dépôt (dossier ignoré par Git).
set -euo pipefail

R="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=${IMAGE:-sh3vita-sdk:local}
CACHE=${CACHE:-sh3vita-build}

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "== construction de l'image $IMAGE (vitaGL compilée à la révision qualifiée, ~5-10 min)"
    docker build -t "$IMAGE" -f "$R/docker/vita-sdk.Dockerfile" "$R"
fi

if ! ls "$R"/port/gen-sh3/*.c >/dev/null 2>&1; then
    echo "port/gen-sh3/ is empty: generate the guest C first (python3 recomp/x86rec.py --target sh3)." >&2
    echo "Pour valider seulement la chaîne de compilation : docker/smoke.sh" >&2
    exit 1
fi

docker volume create "$CACHE" >/dev/null

docker run --rm -i \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e B=/build \
    -v "$R:/work" -v "$CACHE:/build" -w /work/port \
    "$IMAGE" bash -euo pipefail -c '
        export VITASDK=${VITASDK:-/usr/local/vitasdk}
        export PATH="$VITASDK/bin:$PATH"
        bash ./build.sh "$@"
    ' -- "$@"

ls -la "$R/build/"*.vpk
