#!/usr/bin/env python3
"""Estimate word counts from unsegmented text by repeated segmentation (hard EM).

Start from a vocabulary (a khseg TSV; counts are ignored unless --use-prior),
then repeat:
  1. segment the corpus with the current dictionary (khseg, Viterbi)
  2. count how often each vocabulary word was chosen
  3. write a dictionary with those counts
  4. score it on a dev gold file (khseg-eval)
Iteration 0 gives every word the same count, so Viterbi picks the path with
the fewest words, which is close to maximal matching. The dictionary with the
best dev F1 is written to --out.

Unknown spans that occur at least --candidate-min times are written to
--candidates for a person to review. They are never added automatically.

    python scripts/build_freq.py --vocab data/work/lb.tsv --corpus data/work/alt.raw.txt \\
        --dev data/work/khpos.train.all.dev.txt --out data/work/lb.em.tsv \\
        --bin build/mingw-release/tools
"""

import argparse
import collections
import pathlib
import subprocess
import sys


def read_vocab(path: pathlib.Path) -> dict:
    vocab = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        word, _, value = line.partition("\t")
        try:
            vocab[word.strip()] = float(value) if value.strip() else 0.0
        except ValueError:
            vocab[word.strip()] = 0.0
    return vocab


def write_dict(path: pathlib.Path, counts: dict, note: str) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# khseg dictionary, {note}\n# format: count\n")
        for w, c in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
            f.write(f"{w}\t{c:g}\n" if c else f"{w}\n")


def is_khmer(word: str) -> bool:
    return bool(word) and 0x1780 <= ord(word[0]) <= 0x17B3


def dev_f1(exe: pathlib.Path, dev: pathlib.Path, dictionary: pathlib.Path) -> float:
    out = subprocess.run([str(exe), "--gold", str(dev), "--dict", str(dictionary), "--bootstrap", "0",
                          "--tsv", "x"], capture_output=True, text=True, encoding="utf-8", check=True)
    row = out.stdout.strip().splitlines()[-1].split("\t")
    return float(row[3])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vocab", required=True, type=pathlib.Path)
    ap.add_argument("--corpus", required=True, type=pathlib.Path,
                    help="unsegmented text; with --corpus-is-gold, spaces are removed first")
    ap.add_argument("--corpus-is-gold", action="store_true")
    ap.add_argument("--dev", required=True, type=pathlib.Path)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--iterations", type=int, default=4)
    ap.add_argument("--use-prior", action="store_true", help="start from the counts in --vocab")
    ap.add_argument("--candidates", type=pathlib.Path)
    ap.add_argument("--candidate-min", type=int, default=20)
    ap.add_argument("--bin", type=pathlib.Path, required=True, help="directory with khseg and khseg-eval")
    args = ap.parse_args()

    suffix = ".exe" if sys.platform == "win32" else ""
    khseg = args.bin / f"khseg{suffix}"
    evaluate = args.bin / f"khseg-eval{suffix}"
    work = args.out.parent
    work.mkdir(parents=True, exist_ok=True)

    vocab = read_vocab(args.vocab)
    corpus = args.corpus.read_text(encoding="utf-8-sig")
    if args.corpus_is_gold:
        corpus = "\n".join("".join(l.split()) for l in corpus.splitlines()) + "\n"

    counts = dict(vocab) if args.use_prior else {w: 1.0 for w in vocab}
    best_f1, best_iter = -1.0, -1
    unknown: collections.Counter = collections.Counter()
    for it in range(args.iterations + 1):
        current = work / f"{args.out.stem}.iter{it}.tsv"
        write_dict(current, counts, f"hard EM iteration {it}")
        f1 = dev_f1(evaluate, args.dev, current)
        print(f"iteration {it}: dev F1 {f1:.2f}", file=sys.stderr)
        if f1 > best_f1:
            best_f1, best_iter = f1, it
            args.out.write_text(current.read_text(encoding="utf-8"), encoding="utf-8", newline="\n")
        if it == args.iterations:
            break

        seg = subprocess.run([str(khseg), "--dict", str(current)], input=corpus, capture_output=True,
                             text=True, encoding="utf-8", check=True)
        tally: collections.Counter = collections.Counter(seg.stdout.split())
        counts = {w: float(tally.get(w, 0)) for w in vocab}
        unknown = collections.Counter({w: c for w, c in tally.items() if w not in vocab and is_khmer(w)})

    print(f"best: iteration {best_iter}, dev F1 {best_f1:.2f} -> {args.out}", file=sys.stderr)
    if args.candidates:
        with open(args.candidates, "w", encoding="utf-8", newline="\n") as f:
            f.write("# unknown spans from the last segmentation, for manual review\n")
            for w, c in unknown.most_common():
                if c < args.candidate_min:
                    break
                f.write(f"{w}\t{c}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
