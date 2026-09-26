#!/usr/bin/env python3
"""Convert khPOS "word/TAG" lines to khseg gold format (words separated by spaces).

khPOS marks the parts of some words: "~" joins a prefix such as ការ~ or
លោក~ to its word, and "_" joins the parts of a compound (ក្រៅ_ពី). By
default both are treated as one word and the marker is removed. Use
--compounds split to cut at the markers instead, which gives finer words.

    python scripts/convert_khpos.py data/external/khpos/OPEN-TEST data/work/khpos.test.txt
"""

import argparse
import sys


def convert_line(line: str, split: bool) -> str:
    words = []
    for tok in line.split():
        word, sep, _tag = tok.rpartition("/")
        if not sep:
            word = tok
        word = word.replace("​", "")
        if split:
            words.extend(p for p in word.replace("~", " ").replace("_", " ").split() if p)
        else:
            joined = word.replace("~", "").replace("_", "")
            if joined:
                words.append(joined)
            elif word:  # the token is only "~" or "_" itself
                words.append(word)
    return " ".join(words)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--compounds", choices=["join", "split"], default="join")
    args = ap.parse_args()

    n = 0
    with open(args.input, encoding="utf-8-sig") as fin, \
            open(args.output, "w", encoding="utf-8", newline="\n") as fout:
        for line in fin:
            fout.write(convert_line(line, args.compounds == "split") + "\n")
            n += 1
    print(f"{args.output}: {n} lines", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
