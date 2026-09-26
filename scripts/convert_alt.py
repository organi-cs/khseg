#!/usr/bin/env python3
"""Convert the ALT Khmer "nova" files to khseg gold format and split them.

data_km.km-tok.nova has one sentence per line: "SNT.id<TAB>tokens". The
tokens are small units; data_km.km-tag.nova groups them into compounds with
brackets, e.g. "n[n n]n" is one compound noun made of two tokens.

--level atom      one gold word per token (finest)
--level compound  one gold word per bracketed group (closer to khPOS words)

Sentences are split into train/dev/test (80/10/10) by a hash of the
sentence id, so the split is stable across runs and machines.

    python scripts/convert_alt.py data/external/alt/km-nova data/work/alt --level compound
"""

import argparse
import hashlib
import pathlib
import sys


def bucket(sid: str) -> str:
    h = int(hashlib.sha1(sid.encode("utf-8")).hexdigest(), 16) % 10
    return "test" if h == 0 else "dev" if h == 1 else "train"


def group(tokens: list[str], tags: list[str]) -> list[str]:
    """Join tokens that sit inside one top-level bracket group."""
    words, current, depth = [], [], 0
    if len(tokens) != len(tags):
        raise ValueError("token and tag counts differ")
    for tok, tag in zip(tokens, tags):
        opens, closes = tag.count("["), tag.count("]")
        if depth == 0 and opens == 0:
            words.append(tok)
            continue
        current.append(tok)
        depth += opens - closes
        if depth <= 0:
            words.append("".join(current))
            current, depth = [], 0
    if current:
        words.append("".join(current))
    return words


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("nova_dir")
    ap.add_argument("out_prefix")
    ap.add_argument("--level", choices=["atom", "compound"], default="compound")
    args = ap.parse_args()

    nova = pathlib.Path(args.nova_dir)
    tok_lines = (nova / "data_km.km-tok.nova").read_text(encoding="utf-8-sig").splitlines()
    tag_lines = (nova / "data_km.km-tag.nova").read_text(encoding="utf-8-sig").splitlines()
    tags = {}
    for line in tag_lines:
        sid, _, rest = line.partition("\t")
        tags[sid] = rest.split()

    out = {name: [] for name in ("train", "dev", "test")}
    bad = 0
    for line in tok_lines:
        sid, _, rest = line.partition("\t")
        tokens = [t.replace("​", "") for t in rest.split()]
        tokens = [t for t in tokens if t]
        if not tokens:
            continue
        if args.level == "compound":
            try:
                words = group(tokens, tags.get(sid, []))
            except ValueError:
                bad += 1
                continue
        else:
            words = tokens
        out[bucket(sid)].append(" ".join(words))

    prefix = pathlib.Path(args.out_prefix)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    for name, lines in out.items():
        path = prefix.parent / f"{prefix.name}.{args.level}.{name}.txt"
        path.write_text("".join(l + "\n" for l in lines), encoding="utf-8", newline="\n")
        print(f"{path}: {len(lines)} sentences", file=sys.stderr)
    if bad:
        print(f"skipped {bad} sentences whose tag line did not match", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
