# Reprise du projet (sh3-port)

Règles héritées du dépôt SH4 (`../sh3-vita/AGENTS.md`), adaptées à SH3.

- Avant de modifier le code, lancer un test ou utiliser l'émulateur, lire
  `docs/PORTAGE-SH3-2026-11-11.md` (§9 jalons, §11 décisions) puis le dernier
  `docs/HANDOFF.md` s'il existe.
- **Oracle** : `game/sh3.exe` SHA-256 `8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918`
  (version 2003 du dossier fourni ; aucune version Steam/GOG n'existe).
- **Banc** : PC + émulateur Vita3K+ (`../sh2-vita/useful_resources/Vita3K+-x86_64-v1.3.3.AppImage`,
  non 100 % fidèle). Aucun résultat d'émulateur ne vaut validation console.
  Une Vita physique pourra être utilisée plus tard (décision 2026-11-11).
- Distinguer source, candidat compilé, version installée, paquet des testeurs
  et publication. Ne pas annoncer de gain ou de validation sans preuve.
- Ne pas versionner `game/`, `port/gen*/`, `re/out/` ni les preuves sous `build/`.
- Commits uniquement sous `vallfrr <valentin.fortier51110@gmail.com>`, auteur
  et committer, sans co-auteur ajouté. Pas de sous-agents, d'automatisme
  récurrent ni de publication sans accord de Valentin.
- Réponses en anglais avec Valentin (instruction du 2026-11-11).
