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

`golden/` is empty until someone produces the first image on a machine that has the paks. Until
then `make smoke` saves a candidate in `out/smoke.png` and exits 1.

## What gate G1 actually checks

Any one of these passing alone would prove nothing, so the gate requires all of them:

- the modules this tree built are present, because the engine otherwise falls back to the
  bytecode inside `pak0.pk3` and the gate would test nothing of the game code;
- the engine exits successfully, so a crash after the frames are written cannot read as a pass;
- the engine prints its timedemo result, which is the only evidence the demo played to the end;
- at least `MIN_FRAMES` frames were captured, so the comparison frame exists;
- that frame matches `golden/smoke.tga` with zero differing pixels.

Frames come from `cl_avidemo`, which writes one screenshot per rendered frame and only while the
client is active. The Nth captured frame is therefore the Nth frame of demo playback, whatever
the map load cost. Selecting a frame with `wait` instead would move the frame around, because
`wait` counts loading frames too. Override `COMPARE_FRAME` and `MIN_FRAMES` to change the
selection.
