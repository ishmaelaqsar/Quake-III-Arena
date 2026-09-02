# Game data

Copy `pak0.pk3` to `pak8.pk3` from a Quake III Arena installation into this directory. The
development container mounts it read-only at `/paks`, and the smoke gates read it through
`Q3_PAKS`. Git ignores `*.pk3`, so the paks never enter the repository.

You can also leave this directory empty and set `Q3_PAKS` to another directory:

```sh
Q3_PAKS=/path/to/quake3/baseq3 make smoke
```
