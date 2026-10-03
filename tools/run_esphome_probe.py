"""Run compile-only ShelfClock probes with isolated caches and a disk preflight."""
import argparse
import importlib.metadata
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    default_cache = Path(os.environ.get(
        "SHELFCLOCK_ESPHOME_CACHE",
        str(root / ".cache" / "esphome"),
    ))
    parser.add_argument("--cache-root", type=Path, default=default_cache)
    parser.add_argument("--target", choices=("core", "dependency", "runtime"), default="core")
    parser.add_argument("--variant", choices=("test", "full"), default="test")
    parser.add_argument(
        "--substitution",
        action="append",
        nargs=2,
        metavar=("KEY", "VALUE"),
        default=[],
        help="Override an ESPHome substitution; may be supplied more than once.",
    )
    parser.add_argument("--generate", action="store_true")
    args = parser.parse_args()
    if importlib.metadata.version("esphome") != "2026.9.0":
        parser.error("Use the environment installed from tools/requirements-esphome.txt")

    cache = args.cache_root.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(cache).free
    if free < 8 * 1024**3:
        parser.error(f"{cache} has {free / 1024**3:.1f} GiB free; allow at least 8 GiB")
    for name in ("tmp", "logs"):
        (cache / name).mkdir(exist_ok=True)

    env = os.environ.copy()
    env.update({
        "ESPHOME_ESP_IDF_PREFIX": str(cache / "idf"),
        "ESPHOME_BUILD_PATH": str(cache / "build"),
        "ESPHOME_DATA_DIR": str(cache / "data"),
        "PLATFORMIO_CORE_DIR": str(cache / "platformio"),
        "TEMP": str(cache / "tmp"),
        "TMP": str(cache / "tmp"),
    })
    portable_git = cache / "git" / "cmd"
    if (portable_git / "git.exe").is_file():
        env["PATH"] = str(portable_git) + os.pathsep + env.get("PATH", "")
    if args.target == "runtime":
        config_name = f"shelfclock-{args.variant}.yaml"
    else:
        config_name = f"{args.target}-probe.yaml"
    config = root / "esphome" / config_name
    command = [sys.executable, "-m", "esphome"]
    for key, value in args.substitution:
        command.extend(("-s", key, value))
    command.append("compile")
    if args.generate:
        command.append("--only-generate")
    command.append(str(config))
    suffix = "generate" if args.generate else "build"
    log_name = f"{args.target}-{args.variant}-{suffix}.log" if args.target == "runtime" else f"{args.target}-{suffix}.log"
    log = cache / "logs" / log_name
    print(f"Compile-only probe. Log: {log}", flush=True)
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(command, cwd=root, env=env, stdout=output,
                                stderr=subprocess.STDOUT, check=False)
    tail = "\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-25:])
    print(tail.encode("ascii", errors="replace").decode("ascii"))
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
