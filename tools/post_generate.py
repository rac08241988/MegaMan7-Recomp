#!/usr/bin/env python3
"""Post-generate step run by the one-click launcher builder.

The snesrecomp codegen host runs this after `snesrecomp generate`, so the
launcher produces the same tree as tools/regen.sh.

Mega Man 7 has no generated-code hooks yet. The Demon's
Crest template injected its widescreen hooks here
(tools/dc_apply_widescreen_hooks.py); those anchors do not exist in this
game's code, so running that injector made the step exit 1 and the
launcher's "Generate & rebuild" fail. Until this port has hooks of its own,
the step only reports and succeeds.
"""
import argparse
import pathlib
import sys
import traceback

_LOG = None


def log(msg: str) -> None:
    """Print, and append to build/snesrecomp_rebuild.log: the launcher does
    not capture this script's output, so a failure here was silent."""
    print(msg, flush=True)
    if _LOG is not None:
        try:
            with open(_LOG, "a", encoding="utf-8") as f:
                f.write(msg + "\n")
        except OSError:
            pass


def main() -> int:
    global _LOG
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--project-root", default=".")
    ap.add_argument("--gen-dir", default="src/gen")
    args = ap.parse_args()
    root = pathlib.Path(args.project_root).resolve()
    _LOG = root / "build" / "snesrecomp_rebuild.log"
    gen = pathlib.Path(args.gen_dir)
    if not gen.is_absolute():
        gen = root / gen
    if not gen.is_dir():
        log(f"post_generate: ERROR: generated dir {gen} is missing")
        return 1
    log(f"post_generate: ok, nothing to inject for Mega Man 7 ({gen})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except BaseException:
        log("post_generate: ERROR:\n" + traceback.format_exc())
        raise SystemExit(1)
