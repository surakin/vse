# unicode_data_gen

Dev-time only tool (never linked or shipped in the engine/game binary —
same category as CMake itself, `PLAN.md` assumption #8). Generates
`engine/foundation/generated/grapheme_break_table.cpp` from Unicode's
published `GraphemeBreakProperty.txt` and `emoji-data.txt` data files.

The generated output is checked into the repo and is what Foundation code
actually depends on at runtime — this script is not run as part of the
build.

## Pinned version

`/UNICODE_VERSION` (repo root) records the currently pinned version
(`17.0.0`). `downloads/` vendors that version's exact source files so
regeneration doesn't depend on unicode.org still serving the same
snapshot at the same URL indefinitely.

## Regenerating

To retarget a different Unicode version, fetch the new version's data
files and re-run the generator:

```sh
curl -s https://www.unicode.org/Public/<version>/ucd/auxiliary/GraphemeBreakProperty.txt \
  -o downloads/GraphemeBreakProperty.txt
curl -s https://www.unicode.org/Public/<version>/ucd/emoji/emoji-data.txt \
  -o downloads/emoji-data.txt

python3 generate_grapheme_table.py \
  downloads/GraphemeBreakProperty.txt downloads/emoji-data.txt \
  <version> ../../engine/foundation/generated/grapheme_break_table.cpp
```

Then update `/UNICODE_VERSION` to match and commit both the vendored
downloads and the regenerated table together.
