# Resuming the project (sh3-port)

Rules inherited from the SH4 repo (`../sh3-vita/AGENTS.md`), adapted to SH3.

- Before changing code, running a test or using the emulator, read
  `docs/PORTAGE-SH3-2026-11-11.md` (§9 milestones, §11 decisions) and then the
  latest `docs/HANDOFF.md` if it exists.
- **Oracle**: `game/sh3.exe` SHA-256 `8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918`
  (the 2003 version from the supplied folder; no Steam/GOG version exists).
- **Bench**: PC + Vita3K+ emulator (`../sh2-vita/useful_resources/Vita3K+-x86_64-v1.3.3.AppImage`,
  not 100 % faithful). No emulator result counts as console validation.
  A real Vita may be used later (decision 2026-11-11).
- Distinguish source, compiled candidate, installed version, tester package
  and publication. Never claim a gain or a validation without proof.
- Do not version `game/`, `port/gen*/`, `re/out/` or the evidence under `build/`.
- Commits (updated 2026-11-12, by Angelo Galavotti): author and committer are
  `Angelo Galavotti <agalavottib@gmail.com>`. Every commit message has a
  description written in Heather Mason's voice (Silent Hill 3), keeping all
  technical facts intact, and ends with `Co-Authored-By` trailers for Heather
  (`Heather Mason <heather@silent-hill.invalid>`) and for the model that did the
  work: Qwen Flash Next (`<noreply@qwen.ai>`) for local-model work, Claude
  Sonnet 5.5 (`<noreply@anthropic.com>`) for Sonnet work, and the matching
  Claude tag for Opus later. Older commits by `vallfrr` keep their author.
  No subagents, recurring automation or publishing without Angelo's agreement.
- Replies in English with Angelo (updated 2026-11-17; the 2026-11-11
  instruction said Valentin).
