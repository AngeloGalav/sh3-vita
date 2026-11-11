# Conteneur de compilation Vita — portage SH3 (et SH4) de ce dépôt.
#
# Base : image officielle vitasdk (Ubuntu 24.04, GCC 15.2.0, stubs Sce, SDL2,
# vitashark, taihen, mathneon, et une vitaGL « ordinaire »).
#
# Cette image reconstruit vitaGL à la révision exacte utilisée par le port SH4
# (2bdbe89…) avec NO_DEBUG=1 STORE_DEPTH_STENCIL=1, puis applique les patchs du
# dépôt. Sans cela, la garde de port/build.sh refuse la bibliothèque : la
# correction de profondeur évite la disparition des acteurs quand le jeu change
# de cible de rendu (éclairage, ombres, flou, menus).
#
# Construction (contexte = racine du dépôt) :
#   docker build -t sh3vita-sdk:local -f docker/vita-sdk.Dockerfile .
FROM vitasdk/vitasdk:latest

ARG VITAGL_URL=https://github.com/Rinnegatamante/vitaGL.git
# Révision de base des patchs du dépôt (voir docs/WORKFLOW.md et docs/PREPARED-DATA.md).
ARG VITAGL_REV=2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8
# Patchs à appliquer, séparés par des espaces, depuis port/patches/.
# Ne pas empiler vitagl-shared-depth.patch et vitagl-sh4-cache.patch sans raison documentée.
ARG VITAGL_PATCHES="vitagl-shared-depth.patch"
# Options de compilation vitaGL (les mêmes que le candidat SH4 qualifié).
ARG VITAGL_FLAGS="NO_DEBUG=1 STORE_DEPTH_STENCIL=1"

COPY port/patches/ /tmp/vitagl-patches/

RUN set -eux; \
    git clone --no-checkout "${VITAGL_URL}" /tmp/vitaGL; \
    cd /tmp/vitaGL; \
    git checkout "${VITAGL_REV}"; \
    for p in ${VITAGL_PATCHES}; do \
        echo "patch vitaGL: $p"; \
        git apply --verbose "/tmp/vitagl-patches/$p"; \
    done; \
    # shellcheck disable=SC2086
    make -B ${VITAGL_FLAGS} -j"$(nproc)"; \
    make install; \
    cd /; rm -rf /tmp/vitaGL /tmp/vitagl-patches; \
    # Contrôle : la garde de port/build.sh exige les deux modes de conservation.
    vgl="$(arm-vita-eabi-gcc -print-sysroot)/lib/libvitaGL.a"; \
    test -f "$vgl"; \
    arm-vita-eabi-nm -u "$vgl" | grep -F sceGxmDepthStencilSurfaceSetForceLoadMode; \
    arm-vita-eabi-nm -u "$vgl" | grep -F sceGxmDepthStencilSurfaceSetForceStoreMode

# Le SDK de l'image est installé là ; port/build.sh s'appuie sur $VITASDK.
ENV VITASDK=/usr/local/vitasdk
ENV PATH=${VITASDK}/bin:${PATH}

WORKDIR /work
CMD ["bash"]
