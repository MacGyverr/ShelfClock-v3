#!/usr/bin/env python3
"""Verify ShelfClock LED geometry maps without compiling firmware."""

from __future__ import annotations

import ast
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


SEGMENTS_PER_NUMBER = 7
NUMBER_OF_DIGITS = 7
SPECTRUM_PIXELS = 37
SPOT_LEDS = NUMBER_OF_DIGITS * 2
SUPPORTED_LEDS_PER_SEGMENT = range(1, 11)
ACTIVE_PROFILES = {
    4: "test-environment active profile",
    7: "full-clock active profile",
}

DEFINE_RE = re.compile(r"^\s*#define\s+([A-Za-z_]\w*)\s+(.+?)\s*$", re.MULTILINE)
ARRAY_RE = re.compile(
    r"(?:static\s+)?const\s+"
    r"(?:uint8_t|uint16_t|uint32_t|int|byte)\s+"
    r"([A-Za-z_]\w*)\s*\[\s*([^\]]+?)\s*\]\s*=\s*\{(.*?)\};",
    re.DOTALL,
)


@dataclass(frozen=True)
class ArrayDef:
    name: str
    size_expr: str
    initializer: str
    pos: int
    line: int


class VerificationError(Exception):
    pass


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def parse_defines(source: str) -> dict[str, str]:
    macros: dict[str, str] = {}
    for name, value in DEFINE_RE.findall(source):
        macros[name] = strip_comments(value).strip()
    return macros


def parse_arrays(source: str) -> list[ArrayDef]:
    arrays: list[ArrayDef] = []
    for match in ARRAY_RE.finditer(source):
        arrays.append(
            ArrayDef(
                name=match.group(1),
                size_expr=match.group(2).strip(),
                initializer=match.group(3),
                pos=match.start(),
                line=source.count("\n", 0, match.start()) + 1,
            )
        )
    return arrays


def eval_size_expr(expr: str, constants: dict[str, int]) -> int:
    node = ast.parse(expr, mode="eval")

    def walk(part: ast.AST) -> int:
        if isinstance(part, ast.Expression):
            return walk(part.body)
        if isinstance(part, ast.Constant) and isinstance(part.value, int):
            return int(part.value)
        if isinstance(part, ast.Name):
            if part.id not in constants:
                raise VerificationError(f"unknown size constant {part.id!r} in {expr!r}")
            return constants[part.id]
        if isinstance(part, ast.UnaryOp) and isinstance(part.op, ast.USub):
            return -walk(part.operand)
        if isinstance(part, ast.BinOp):
            left = walk(part.left)
            right = walk(part.right)
            if isinstance(part.op, ast.Add):
                return left + right
            if isinstance(part.op, ast.Sub):
                return left - right
            if isinstance(part.op, ast.Mult):
                return left * right
            if isinstance(part.op, (ast.Div, ast.FloorDiv)):
                if right == 0:
                    raise VerificationError(f"division by zero in {expr!r}")
                return left // right
        raise VerificationError(f"unsupported size expression {expr!r}")

    return walk(node)


def constants_for(leds_per_segment: int) -> dict[str, int]:
    leds_per_digit = leds_per_segment * SEGMENTS_PER_NUMBER
    fake_num_leds = NUMBER_OF_DIGITS * leds_per_digit
    segments_leds = SPECTRUM_PIXELS * leds_per_segment
    return {
        "SEGMENTS_PER_NUMBER": SEGMENTS_PER_NUMBER,
        "NUMBER_OF_DIGITS": NUMBER_OF_DIGITS,
        "SPECTRUM_PIXELS": SPECTRUM_PIXELS,
        "LEDS_PER_SEGMENT": leds_per_segment,
        "LEDS_PER_DIGIT": leds_per_digit,
        "FAKE_NUM_LEDS": fake_num_leds,
        "SEGMENTS_LEDS": segments_leds,
        "SPOT_LEDS": SPOT_LEDS,
        "NUM_LEDS": segments_leds + SPOT_LEDS,
        "SOUNDDETECTOR_BANDS_WIDTH": 8,
        "ANALYZER_SIZE": 8 * leds_per_segment * 2,
    }


def expand_call(name: str, segment: int, leds_per_segment: int, segment_mode: bool) -> list[int]:
    if segment_mode:
        return [segment]
    base = segment * leds_per_segment
    values = list(range(base, base + leds_per_segment))
    if name == "nseg":
        values.reverse()
    return values


def expand_initializer(
    expression: str,
    leds_per_segment: int,
    macros: dict[str, str],
    *,
    segment_mode: bool = False,
    stack: tuple[str, ...] = (),
) -> list[int]:
    values: list[int] = []
    cleaned = strip_comments(expression)

    for raw_token in cleaned.split(","):
        token = raw_token.strip()
        if not token:
            continue

        call = re.fullmatch(r"(n?seg)\s*\(\s*(\d+)\s*\)", token)
        if call:
            values.extend(
                expand_call(call.group(1), int(call.group(2)), leds_per_segment, segment_mode)
            )
            continue

        if token in macros:
            if token in stack:
                chain = " -> ".join((*stack, token))
                raise VerificationError(f"recursive macro expansion: {chain}")
            values.extend(
                expand_initializer(
                    macros[token],
                    leds_per_segment,
                    macros,
                    segment_mode=segment_mode,
                    stack=(*stack, token),
                )
            )
            continue

        if re.fullmatch(r"\d+", token):
            values.append(int(token))
            continue

        raise VerificationError(f"cannot expand token {token!r}")

    return values


def describe_repeats(values: list[int]) -> tuple[list[int], list[int]]:
    counts = Counter(values)
    repeats = sorted(value for value, count in counts.items() if count > 1)
    missing = sorted(set(range(SPECTRUM_PIXELS)) - set(values))
    return repeats, missing


def ensure_array(
    arrays_by_name: dict[str, list[ArrayDef]], name: str, failures: list[str]
) -> ArrayDef | None:
    matches = arrays_by_name.get(name, [])
    if not matches:
        failures.append(f"missing array {name}")
        return None
    return matches[0]


def check_declared_size(
    array: ArrayDef,
    expected: int,
    constants: dict[str, int],
    profile_name: str,
    failures: list[str],
) -> None:
    try:
        declared = eval_size_expr(array.size_expr, constants)
    except VerificationError as exc:
        failures.append(f"{profile_name} {array.name}: {exc}")
        return
    if declared != expected:
        failures.append(
            f"{profile_name} {array.name}: declaration {array.size_expr!r} "
            f"evaluates to {declared}, expected {expected}"
        )


def check_len_and_bounds(
    *,
    values: list[int],
    expected_len: int,
    max_exclusive: int,
    label: str,
    profile_name: str,
    failures: list[str],
) -> bool:
    ok = True
    if len(values) != expected_len:
        failures.append(f"{profile_name} {label}: length {len(values)}, expected {expected_len}")
        ok = False
    out_of_bounds = [value for value in values if value < 0 or value >= max_exclusive]
    if out_of_bounds:
        shown = sorted(set(out_of_bounds))
        failures.append(
            f"{profile_name} {label}: values outside 0..{max_exclusive - 1}: {shown}"
        )
        ok = False
    return ok


def check_profile(
    *,
    profile_name: str,
    leds_per_segment: int,
    macros: dict[str, str],
    arrays_by_name: dict[str, list[ArrayDef]],
    failures: list[str],
) -> list[str]:
    lines: list[str] = []
    constants = constants_for(leds_per_segment)
    segments_leds = constants["SEGMENTS_LEDS"]

    lines.append(f"\nProfile: {profile_name} (LEDS_PER_SEGMENT={leds_per_segment})")
    lines.append(
        "  derived: LEDS_PER_DIGIT={LEDS_PER_DIGIT}, FAKE_NUM_LEDS={FAKE_NUM_LEDS}, "
        "SEGMENTS_LEDS={SEGMENTS_LEDS}, SPOT_LEDS={SPOT_LEDS}, NUM_LEDS={NUM_LEDS}, "
        "ANALYZER_SIZE={ANALYZER_SIZE}".format(**constants)
    )

    analyzer = ensure_array(arrays_by_name, "ANALYZER", failures)
    if analyzer is not None:
        check_declared_size(
            analyzer, constants["ANALYZER_SIZE"], constants, profile_name, failures
        )
        try:
            analyzer_values = expand_initializer(
                analyzer.initializer, leds_per_segment, macros
            )
        except VerificationError as exc:
            failures.append(f"{profile_name} ANALYZER: {exc}")
        else:
            if check_len_and_bounds(
                values=analyzer_values,
                expected_len=constants["ANALYZER_SIZE"],
                max_exclusive=segments_leds,
                label="ANALYZER",
                profile_name=profile_name,
                failures=failures,
            ):
                lines.append(
                    f"  OK ANALYZER: {len(analyzer_values)} LED indexes within 0..{segments_leds - 1}"
                )

    fake_leds = ensure_array(arrays_by_name, "FAKE_LEDs", failures)
    if fake_leds is not None:
        check_declared_size(fake_leds, constants["FAKE_NUM_LEDS"], constants, profile_name, failures)
        try:
            fake_values = expand_initializer(fake_leds.initializer, leds_per_segment, macros)
            fake_segments = expand_initializer(
                fake_leds.initializer, leds_per_segment, macros, segment_mode=True
            )
        except VerificationError as exc:
            failures.append(f"{profile_name} FAKE_LEDs: {exc}")
        else:
            if check_len_and_bounds(
                values=fake_values,
                expected_len=constants["FAKE_NUM_LEDS"],
                max_exclusive=segments_leds,
                label="FAKE_LEDs",
                profile_name=profile_name,
                failures=failures,
            ):
                repeats, missing = describe_repeats(fake_segments)
                if missing:
                    failures.append(f"{profile_name} FAKE_LEDs: missing physical segments {missing}")
                else:
                    lines.append(
                        f"  OK FAKE_LEDs: {len(fake_values)} LED indexes, covers all "
                        f"{SPECTRUM_PIXELS} physical segments; repeated segments {repeats}"
                    )

    map_names = [
        "FAKE_LEDs_C_BMUP",
        "FAKE_LEDs_C_CMOT",
        "FAKE_LEDs_C_BLTR",
        "FAKE_LEDs_C_TLBR",
        "FAKE_LEDs_C_TMDN",
        "FAKE_LEDs_C_CSIN",
        "FAKE_LEDs_C_BRTL",
        "FAKE_LEDs_C_TRBL",
        "FAKE_LEDs_C_VERT",
        "FAKE_LEDs_C_VERT2",
        "FAKE_LEDs_C_OUTS",
        "FAKE_LEDs_C_OUTS2",
        "FAKE_LEDs_C_FIRE",
        "FAKE_LEDs_C_RAIN",
        "FAKE_LEDs_SNAKE",
    ]
    exact_maps: list[str] = []
    noted_maps: list[str] = []
    for name in map_names:
        array = ensure_array(arrays_by_name, name, failures)
        if array is None:
            continue
        check_declared_size(array, segments_leds, constants, profile_name, failures)
        try:
            values = expand_initializer(array.initializer, leds_per_segment, macros)
            segments = expand_initializer(
                array.initializer, leds_per_segment, macros, segment_mode=True
            )
        except VerificationError as exc:
            failures.append(f"{profile_name} {name}: {exc}")
            continue

        if not check_len_and_bounds(
            values=values,
            expected_len=segments_leds,
            max_exclusive=segments_leds,
            label=name,
            profile_name=profile_name,
            failures=failures,
        ):
            continue

        repeats, missing = describe_repeats(segments)
        if repeats or missing:
            noted_maps.append(f"{name}: duplicate segments {repeats}, missing segments {missing}")
        else:
            exact_maps.append(name)

    if exact_maps:
        lines.append(
            f"  OK segment maps: {len(exact_maps)} exact segment permutations "
            f"({len(exact_maps) * leds_per_segment * SPECTRUM_PIXELS} LED indexes checked)"
        )
    for note in noted_maps:
        lines.append(f"  NOTE {note}")

    cylon = ensure_array(arrays_by_name, "CYLON", failures)
    if cylon is not None:
        check_declared_size(cylon, 12, constants, profile_name, failures)
        try:
            cylon_values = expand_initializer(cylon.initializer, leds_per_segment, macros)
        except VerificationError as exc:
            failures.append(f"{profile_name} CYLON: {exc}")
        else:
            if check_len_and_bounds(
                values=cylon_values,
                expected_len=12,
                max_exclusive=SPECTRUM_PIXELS,
                label="CYLON",
                profile_name=profile_name,
                failures=failures,
            ):
                repeats, _ = describe_repeats(cylon_values)
                lines.append(
                    f"  OK CYLON: 12 segment steps within 0..36; bounce repeats {repeats}"
                )

    return lines


def check_exact_map(
    label: str,
    array: ArrayDef | None,
    expected: list[int],
    constants: dict[str, int],
    macros: dict[str, str],
    failures: list[str],
) -> str | None:
    if array is None:
        failures.append(f"missing {label} idxMap")
        return None

    check_declared_size(array, len(expected), constants, "shared", failures)
    try:
        values = expand_initializer(array.initializer, constants["LEDS_PER_SEGMENT"], macros)
    except VerificationError as exc:
        failures.append(f"shared {label}: {exc}")
        return None

    if len(values) != len(expected):
        failures.append(f"shared {label}: length {len(values)}, expected {len(expected)}")
        return None
    if sorted(values) != list(range(len(expected))):
        failures.append(f"shared {label}: not a permutation of 0..{len(expected) - 1}: {values}")
        return None
    if values != expected:
        failures.append(f"shared {label}: order {values}, expected {expected}")
        return None
    return f"  OK {label}: {values}"


def check_shelf_maps(
    *,
    source: str,
    arrays: list[ArrayDef],
    macros: dict[str, str],
    failures: list[str],
) -> list[str]:
    lines = ["\nShelf mappings:"]
    constants = constants_for(4)

    process_tide_pos = source.find("void processCurrentTide()")
    if process_tide_pos < 0:
        failures.append("missing processCurrentTide")
        process_tide_pos = len(source)

    idx_maps = [array for array in arrays if array.name == "idxMap"]
    rain_maps = [array for array in idx_maps if array.pos < process_tide_pos]
    tide_maps = [array for array in idx_maps if array.pos > process_tide_pos]

    rain_expected = [7, 8, 9, 10, 11, 12, 13, 6, 5, 4, 3, 2, 1, 0]
    tide_test_expected = list(range(14))
    tide_full_expected = rain_expected

    lines.append(
        check_exact_map(
            "rain forecast fetch idxMap",
            rain_maps[-1] if rain_maps else None,
            rain_expected,
            constants,
            macros,
            failures,
        )
        or "  ERROR rain forecast fetch idxMap"
    )

    if len(tide_maps) != 2:
        failures.append(f"processCurrentTide idxMap count {len(tide_maps)}, expected 2")

    lines.append(
        check_exact_map(
            "tide test-environment idxMap",
            tide_maps[0] if len(tide_maps) > 0 else None,
            tide_test_expected,
            constants,
            macros,
            failures,
        )
        or "  ERROR tide test-environment idxMap"
    )
    lines.append(
        check_exact_map(
            "tide full-clock idxMap",
            tide_maps[1] if len(tide_maps) > 1 else None,
            tide_full_expected,
            constants,
            macros,
            failures,
        )
        or "  ERROR tide full-clock idxMap"
    )
    lines.append(
        "  NOTE rain forecast render path writes rainForecast[0..13] directly to shelf "
        "LED slots SEGMENTS_LEDS..SEGMENTS_LEDS+13"
    )
    return lines


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    source_path = repo_root / "src" / "ShelfClock.cpp"
    led_maps_path = repo_root / "include" / "ShelfClockLedMaps.h"
    source_paths = [source_path]
    if led_maps_path.exists():
        source_paths.append(led_maps_path)

    source_parts = [
        path.read_text(encoding="utf-8", errors="replace")
        for path in source_paths
    ]
    source = "\n\n".join(source_parts)
    tide_source = source_parts[0]

    macros = parse_defines(source)
    arrays = parse_arrays(source)
    arrays_by_name: dict[str, list[ArrayDef]] = defaultdict(list)
    for array in arrays:
        arrays_by_name[array.name].append(array)

    failures: list[str] = []
    output: list[str] = [
        "ShelfClock geometry verification",
        "Sources: "
        + ", ".join(str(path.relative_to(repo_root)) for path in source_paths),
    ]

    for leds_per_segment in SUPPORTED_LEDS_PER_SEGMENT:
        profile_name = ACTIVE_PROFILES.get(
            leds_per_segment, f"supported geometry {leds_per_segment}"
        )
        output.extend(
            check_profile(
                profile_name=profile_name,
                leds_per_segment=leds_per_segment,
                macros=macros,
                arrays_by_name=arrays_by_name,
                failures=failures,
            )
        )

    output.extend(
        check_shelf_maps(source=tide_source, arrays=arrays, macros=macros, failures=failures)
    )

    if failures:
        output.append("\nFAILURES:")
        output.extend(f"  - {failure}" for failure in failures)
        print("\n".join(output))
        return 1

    output.append("\nVerification complete: no fatal geometry issues.")
    print("\n".join(output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
