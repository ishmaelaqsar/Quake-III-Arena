# Smoke gates

These scripts are the integration gates that the checklists in `docs/plans/` reference. They
run inside the development container through the Makefile.

| Command | Script | What it proves |
|---|---|---|
| `make smoke` | `run_smoke.sh` | Gate G1: a fixed timedemo frame renders identically to `golden/smoke.tga` under Mesa `llvmpipe`. |
| `make smoke-update-golden` | `run_smoke.sh --update-golden` | Accepts the current frame as the new golden image. Commit it with the reason for the change. |
| `make apitrace` | `run_smoke.sh --apitrace` | Records the GL calls of the smoke run and prints counts for the fixed-function calls that the renderer rewrite removes. |
| `make bot-match` | `run_bot_match.sh` | The dedicated server runs a 60 second match with four bots without an error. Pass `1` as the second argument to load the id QVM instead of the native module. |

All scripts need the retail paks. Put `pak0.pk3` to `pak8.pk3` in `docker/paks/` or set
`Q3_PAKS` to the directory that holds them. Output lands in `out/`, which git ignores.

`golden/` is empty until the C++ preparation pull request produces the first image (checklist
`04-cxx-migration.md`, phase P0). Until then `make smoke` saves a candidate in `out/smoke.png`
and exits 1.
