# Original source tree

What the executable says about how Bungie's source was laid out. `src/` mirrors this as code is
reconstructed.

```
d:\bungie\evil\shared\engine\source\        -> src/evil/shared/engine/source/
    engine\    Tiger engine core
    omaha\     game framework layer (main, game, networking, ...)
    spasaha\   Destiny-specific game layer (Jam target root)
d:\bungie\external_libraries\               identified, not reconstructed
```

Build output path in the binary:
`d:/bungie/evil/objects/jam_orbis_release/engine_source_spasaha_spasaha/bin/tiger_release_final.elf`,
build stamp `56578.17.04.12.1251.evil`.

Third-party libraries linked in: CRIWARE, DemonWare, FreeType, Havok, libtomcrypt, Oodle, the SCE NP
toolkit, SpeedTree and Wwise.

## Evidence levels

- A directory or file is **confirmed** when its path appears verbatim in a string in the binary.
- A subsystem name in `records/names.tsv` (`engine.graphics`, `omaha.world_controller`, ...) is a
  **proposal** from clustering RTTI class names and log channels (`networking:*`, `world_controller:*`).
  Rename it when a real path turns up.
- Class names, vtables and base classes come from Itanium RTTI and are confirmed. Which folder a class
  sits in is a proposal unless a source path says so.

See docs/executable-metadata-rtti.md for how the RTTI and metadata were recovered.
