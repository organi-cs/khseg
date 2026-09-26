#!/usr/bin/env python3
"""Merge word lists and counts into one khseg dictionary TSV.

Each source is KIND:PATH or KIND:PATH:WEIGHT.

  counts  "word<TAB>count" or "word,count" lines (khmerlbdict src/*.txt)
  list    one word per line; adds the words, and WEIGHT to each count
          (default weight 0: known word, no count)
  icu     ICU khmerdict.txt (a list with # comments)
  gold    segmented text, words separated by spaces; each occurrence adds WEIGHT

Counts from all sources are multiplied by their weight and added up. Only
Khmer words that khseg would accept are kept (Khmer letters only, starting
with a consonant or independent vowel); the number dropped is reported.

    python scripts/build_dict.py -o data/work/lb.tsv \\
        counts:data/external/khmerlbdict/seafreq.txt \\
        icu:data/external/icu/khmerdict.txt
"""

import argparse
import collections
import datetime
import pathlib
import sys


def khmer_letter(cp: int) -> bool:
    # Mirrors khseg::is_khmer_letter plus ZWNJ/ZWJ (see include/khseg/charclass.hpp).
    return 0x1780 <= cp <= 0x17D3 or cp in (0x17DC, 0x17DD, 0x200C, 0x200D)


def is_base(cp: int) -> bool:
    return 0x1780 <= cp <= 0x17B3 or cp == 0x17DC


def acceptable(word: str) -> bool:
    return bool(word) and is_base(ord(word[0])) and all(khmer_letter(ord(c)) for c in word)


def clean(word: str) -> str:
    return word.strip().replace("​", "").replace("﻿", "")


def read_source(kind: str, path: pathlib.Path, weight: float, counts: collections.Counter,
                vocab: set) -> tuple[int, int]:
    kept = dropped = 0
    text = path.read_text(encoding="utf-8-sig")

    def add(word: str, count: float) -> None:
        nonlocal kept, dropped
        word = clean(word)
        if not acceptable(word):
            dropped += 1
            return
        kept += 1
        vocab.add(word)
        if count:
            counts[word] += count

    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if kind == "counts":
            sep = "\t" if "\t" in line else ","
            word, _, value = line.partition(sep)
            try:
                c = float(value) if value.strip() else 0.0
            except ValueError:
                dropped += 1
                continue
            add(word, c * weight)
        elif kind in ("list", "icu"):
            add(line.split("\t")[0], weight)
        elif kind == "gold":
            for w in line.split():
                add(w, weight)
    return kept, dropped


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sources", nargs="+")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--min-count", type=float, default=0.0,
                    help="drop words whose total count is below this (words without counts are kept)")
    args = ap.parse_args()

    counts: collections.Counter = collections.Counter()
    vocab: set = set()
    header = ["# khseg dictionary", f"# built {datetime.date.today().isoformat()} by scripts/build_dict.py"]
    for spec in args.sources:
        parts = spec.split(":")
        # Windows paths contain a drive colon, so the weight is only the last
        # part when it parses as a number.
        kind, rest = parts[0], parts[1:]
        weight = None
        if len(rest) > 1:
            try:
                weight = float(rest[-1])
                rest = rest[:-1]
            except ValueError:
                pass
        path = pathlib.Path(":".join(rest))
        if kind not in ("counts", "list", "icu", "gold"):
            print(f"unknown source kind '{kind}'", file=sys.stderr)
            return 1
        if weight is None:
            weight = 0.0 if kind in ("list", "icu") else 1.0
        kept, dropped = read_source(kind, path, weight, counts, vocab)
        print(f"{path}: kept {kept}, dropped {dropped}", file=sys.stderr)
        header.append(f"# source {kind}:{path.name} weight {weight:g}")

    words = sorted(vocab, key=lambda w: (-counts.get(w, 0.0), w))
    if args.min_count > 0:
        words = [w for w in words if w not in counts or counts[w] >= args.min_count]
    header.append("# format: count")

    out = pathlib.Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(header) + "\n")
        for w in words:
            c = counts.get(w, 0.0)
            f.write(f"{w}\t{c:g}\n" if c else f"{w}\n")
    print(f"{out}: {len(words)} words, total count {sum(counts.values()):g}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
