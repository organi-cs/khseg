#!/usr/bin/env python3
"""Split a gold file into train and dev parts by a hash of each line.

The same line always lands in the same part, so the split does not depend on
file order and is the same on every machine.

    python scripts/split.py data/work/khpos.train.all.txt --dev-percent 10
    -> data/work/khpos.train.all.train.txt, data/work/khpos.train.all.dev.txt
"""

import argparse
import hashlib
import pathlib
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("--dev-percent", type=int, default=10)
    args = ap.parse_args()

    src = pathlib.Path(args.input)
    train, dev = [], []
    for line in src.read_text(encoding="utf-8-sig").splitlines():
        if not line.strip():
            continue
        h = int(hashlib.sha1(line.encode("utf-8")).hexdigest(), 16) % 100
        (dev if h < args.dev_percent else train).append(line)

    for name, lines in (("train", train), ("dev", dev)):
        out = src.with_name(f"{src.stem}.{name}.txt")
        out.write_text("".join(l + "\n" for l in lines), encoding="utf-8", newline="\n")
        print(f"{out}: {len(lines)} lines", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
