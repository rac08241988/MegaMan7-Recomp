# recomp-updater

A small, reusable C helper that SNES Recomp ports use for the in-game
**Check for Updates** row.

## What it does

- `GET https://api.github.com/repos/<owner>/<repo>/releases/latest`
- Reads `tag_name` and `html_url`
- Compares that tag with the build's version
- Reports **Up to date** or **Update available vX**
- Can open the release page in the default browser

It does not download a zip, patch an install, or run an installer. The
player follows the release page themselves.

`third_party/miniz` is kept here for a future optional download path. This
version does not link it.

## Use from a game

1. Copy `recomp_updater.h` and `recomp_updater.c` into the game tree
   (this repo uses `src/updater/`) so the player's first-run rebuild has
   the sources in the setup zip.
2. Add them to the executable target. On Windows, link `winhttp` and
   `shell32`.
3. Compile in the GitHub repo and the current version:

   ```cmake
   target_compile_definitions(<Target> PRIVATE
       RECOMP_UPDATER_REPO_OWNER="rac08241988"
       RECOMP_UPDATER_REPO_NAME="Uniracers-Recomp")
   ```

   The current version is the same `SNES_GAME_VERSION` the game already
   stamps from its `VERSION` file.
4. From the System tab, call `recomp_updater_check(...)`. If the status
   is `RECOMP_UPDATER_UPDATE_AVAILABLE`, offer to call
   `recomp_updater_open_url(result.release_url)`.

## Testing

- **Up to date:** run a build whose `VERSION` matches the latest GitHub
  release tag (for example `1.0.0` vs `v1.0.0`).
- **Update available:** set `RECOMP_UPDATER_CURRENT_VERSION=0.0.1` in the
  environment and check again. The menu should say `Update available vX`
  and a second confirm should open the release page.
- **Parser / version compare:** `tests/test_updater.c` (no network).

The `RECOMP_UPDATER_CURRENT_VERSION` override is the supported way to
exercise the newer-release path without publishing a fake tag.
