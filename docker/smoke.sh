#!/usr/bin/env bash
# Valide la chaîne de compilation Vita du conteneur, sans code du jeu.
#
# Compile une application vitaGL minimale (fond d'écran rouge, 60 images) et la
# emballe en VPK : cela prouve toolchain + vitaGL patchée + vitashark + ShaccCg
# + empaquetage, bien avant que le C recompilé du jeu existe.
#
#   docker/smoke.sh        # puis build/smoke.vpk
set -euo pipefail

R="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=${IMAGE:-sh3vita-sdk:local}
SRC="$R/build/smoke-src"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "== construction de l'image $IMAGE"
    docker build -t "$IMAGE" -f "$R/docker/vita-sdk.Dockerfile" "$R"
fi

mkdir -p "$SRC"
cat > "$SRC/main.c" <<'EOF'
#include <vitaGL.h>

int main(void) {
    vglInit(8);
    glEnable(GL_DEPTH_TEST);
    for (int i = 0; i < 60; i++) {
        glClearColor(0.20f, 0.03f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);
    }
    return 0;   /* la sortie propre dépend de la cible ; le banc valide compilation, lien et paquet */
}
EOF

cat > "$SRC/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "")
endif()
project(smoke C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "toolchain smoke")
set(VITA_TITLEID  "SMOKE0001")
set(VITA_VERSION  "00.01")
add_executable(smoke main.c)
# Même liste de bibliothèques que port/CMakeLists.txt : le banc doit prouver
# exactement ce que le vrai build exige.
target_link_libraries(smoke vitaGL vitashark SceShaccCgExt mathneon taihen_stub SDL2
  SceDisplay_stub SceCtrl_stub SceAudio_stub SceAudioIn_stub SceSysmodule_stub SceGxm_stub
  SceCommonDialog_stub SceAppMgr_stub SceTouch_stub SceHid_stub SceMotion_stub
  SceKernelDmacMgr_stub SceShaccCg_stub SceIofilemgr_stub SceLibKernel_stub
  SceKernelThreadMgr_stub SceKernelModulemgr_stub SceProcessmgr_stub SceRtc_stub
  ScePower_stub SceIme_stub SceJpeg_stub stdc++ m z)
vita_create_self(eboot.bin smoke UNSAFE)
vita_create_vpk(smoke.vpk ${VITA_TITLEID} eboot.bin VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME})
EOF

docker run --rm -i -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$R:/work" -w /work/build/smoke-src "$IMAGE" \
    bash -euo pipefail -c '
        export VITASDK=${VITASDK:-/usr/local/vitasdk}
        export PATH="$VITASDK/bin:$PATH"
        cmake -S . -B b -DCMAKE_BUILD_TYPE=Release > /dev/null
        cmake --build b -j"$(nproc)"
    '

cp "$SRC/b/smoke.vpk" "$R/build/smoke.vpk"
ls -la "$R/build/smoke.vpk"
sha256sum "$R/build/smoke.vpk"
echo "OK : toolchain Vita + vitaGL patchée opérationnelles dans $IMAGE"
