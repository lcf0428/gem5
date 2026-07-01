#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path


def read_pairs(path: Path):
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    if len(lines) % 2 != 0:
        raise ValueError(f"{path} has odd number of lines: {len(lines)}")

    pairs = []
    for i in range(0, len(lines), 2):
        pairs.append((lines[i], lines[i + 1], i + 1))
    return pairs


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Reorder log2 key/value pairs by the key order in log1. "
            "If a key is missing in log2, fill the position with the log1 pair "
            "and prefix the key with [Not Found]."
        )
    )
    parser.add_argument("log1", type=Path, help="Reference log file: key/value pairs")
    parser.add_argument("log2", type=Path, help="Log file to reorder: key/value pairs")
    parser.add_argument("-o", "--output", type=Path, required=True, help="Output path")
    parser.add_argument(
        "--max-missing",
        type=int,
        default=10,
        help="Stop after missing key count is greater than this value. Default: 10",
    )
    args = parser.parse_args()

    try:
        log1_pairs = read_pairs(args.log1)
        log2_pairs = read_pairs(args.log2)
    except Exception as exc:
        print(f"[ERROR] failed to read input: {exc}", file=sys.stderr)
        return 1

    used = [False] * len(log2_pairs)
    reordered = []
    missing = []

    for ref_idx, (key, value, ref_line_no) in enumerate(log1_pairs, start=1):
        found_idx = None
        for i, (candidate_key, candidate_value, candidate_line_no) in enumerate(log2_pairs):
            if used[i]:
                continue
            if candidate_key == key:
                found_idx = i
                reordered.extend([candidate_key, candidate_value])
                used[i] = True
                break

        if found_idx is None:
            missing.append((ref_idx, ref_line_no, key.rstrip("\n")))
            reordered.extend(["[Not Found]" + key, value])
            if len(missing) > args.max_missing:
                args.output.write_text("".join(reordered), encoding="utf-8")
                print(
                    f"[ERROR] missing key count exceeded {args.max_missing}; "
                    f"wrote partial result to {args.output} and stopped.",
                    file=sys.stderr,
                )
                for missing_ref_idx, missing_ref_line_no, missing_key_preview in missing:
                    print(
                        "  filled from log1: "
                        f"pair_index={missing_ref_idx}, "
                        f"key_line={missing_ref_line_no}, "
                        f"key={missing_key_preview!r}",
                        file=sys.stderr,
                    )
                return 2

    args.output.write_text("".join(reordered), encoding="utf-8")

    unused_count = used.count(False)
    if unused_count:
        print(f"[WARN] {unused_count} unused pair(s) remain in log2.", file=sys.stderr)
    if missing:
        print(f"[WARN] {len(missing)} key(s) from log1 were not found in log2.", file=sys.stderr)
        for ref_idx, ref_line_no, key_preview in missing:
            print(
                f"  filled from log1: pair_index={ref_idx}, key_line={ref_line_no}, key={key_preview!r}",
                file=sys.stderr,
            )
    print(f"[OK] wrote reordered log to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
