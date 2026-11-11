# Conteneur de compilation Vita

Image de compilation pour les ports de ce dépôt (SH4 aujourd'hui, SH3 en préparation),
sans rien installer sur l'hôte en dehors de Docker.

## Fichiers

| Fichier | Rôle |
|---|---|
| `vita-sdk.Dockerfile` | Base `vitasdk/vitasdk:latest` + **vitaGL recompilée** à la révision `2bdbe89…` avec `NO_DEBUG=1 STORE_DEPTH_STENCIL=1` et le patch `port/patches/vitagl-shared-depth.patch`. La dernière étape du build vérifie les deux modes de conservation de profondeur exigés par `port/build.sh`. |
| `build-vita.sh` | Construit l'image si absente, puis lance `port/build.sh` dans le conteneur. Le dossier CMake vit dans un volume Docker : les rebuilds sont incrémentaux. |
| `smoke.sh` | Valide la chaîne **sans code du jeu** : une application vitaGL minimale compilée, liée et empaquetée en `build/smoke.vpk`. |

## Usage

```sh
docker/smoke.sh                    # prouve toolchain + vitaGL patchée + empaquetage
docker/build-vita.sh               # build complet du VPK (nécessite port/gen/)
docker/build-vita.sh -DSH4_GUEST_MB=200 -DSH4_TRACE_CALLS=OFF
```

Variables d'environnement :

- `IMAGE` — nom de l'image (défaut `sh3vita-sdk:local`) ;
- `CACHE` — nom du volume de build (défaut `sh3vita-build`) ;
- à la construction de l'image : `VITAGL_REV`, `VITAGL_PATCHES`, `VITAGL_FLAGS`
  (ne pas empiler `vitagl-shared-depth.patch` et `vitagl-sh4-cache.patch` sans raison documentée,
  voir `docs/PREPARED-DATA.md`).

Le VPK est déposé dans `build/` à la racine du dépôt (ignoré par Git).

## Ce que le conteneur garantit, et ce qu'il ne garantit pas

Garantis : compilateur ARM, vitaGL à la révision des patchs du dépôt avec la correction de
profondeur, vitashark, ShaccCg, empaquetage VPK.

Non garantis : que le C recompilé du jeu existe ou compile (il faut d'abord `recomp/x86rec.py`),
et a fortiori que le jeu tourne. Un VPK qui se construit n'est pas un port validé.

## Espace disque

L'image pèse environ 2,2 Go. Sur une machine déjà pleine, vérifier `df -h /` avant de construire,
et `docker image prune` / `docker volume rm` pour libérer.
