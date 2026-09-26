#!/usr/bin/env python3
"""Reproduce the accuracy table in bench/results/accuracy.md.

Needs the data from scripts/fetch_data.py (icu, khpos, alt, khmerlbdict) and
a Release build of khseg and khseg-eval. Everything it creates goes to
data/work/ except the final table.

    python scripts/fetch_data.py
    python scripts/experiments.py --bin build/mingw-release/tools

Unknown-cluster costs are tuned on dev sets only; test sets are scored once.
"""

import argparse
import datetime
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
X = ROOT / "data" / "external"
W = ROOT / "data" / "work"
PY = sys.executable


def run(cmd, **kw) -> str:
    out = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, encoding="utf-8",
                         check=True, **kw)
    return out.stdout


def prepare(bin_dir: pathlib.Path) -> None:
    W.mkdir(parents=True, exist_ok=True)
    s = ROOT / "scripts"
    run([PY, s / "convert_khpos.py", X / "khpos/train.all", W / "khpos.trainall.txt"])
    run([PY, s / "convert_khpos.py", X / "khpos/OPEN-TEST", W / "khpos.test.txt"])
    run([PY, s / "split.py", W / "khpos.trainall.txt", "--dev-percent", "10"])
    for level in ("compound", "atom"):
        run([PY, s / "convert_alt.py", X / "alt/km-nova", W / "alt", "--level", level])
    lb = X / "khmerlbdict"
    run([PY, s / "build_dict.py", "-o", W / "lb.tsv",
         f"counts:{lb / 'seafreq.txt'}", f"counts:{lb / 'KHSV.txt'}", f"counts:{lb / 'KHOV.txt'}",
         f"list:{lb / 'names.txt'}", f"list:{lb / 'places.txt'}", f"counts:{lb / 'villages.txt'}",
         f"icu:{X / 'icu/khmerdict.txt'}"])
    run([PY, s / "build_dict.py", "-o", W / "icu.tsv", f"icu:{X / 'icu/khmerdict.txt'}"])
    run([PY, s / "build_dict.py", "-o", W / "khpos.train.tsv", f"gold:{W / 'khpos.trainall.train.txt'}"])
    for level in ("compound", "atom"):
        run([PY, s / "build_dict.py", "-o", W / f"alt.{level}.train.tsv",
             f"gold:{W / f'alt.{level}.train.txt'}"])
    # Benchmark input for khseg-bench: every unsegmented sentence of ALT train and khPOS.
    lines = []
    for name in ("alt.compound.train.txt", "khpos.trainall.txt"):
        lines += ["".join(l.split()) for l in (W / name).read_text(encoding="utf-8").splitlines()]
    (W / "bench.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    # Open word lists, counts estimated from ALT text with hard EM, tuned on khPOS dev.
    subprocess.run([PY, s / "build_freq.py", "--vocab", W / "lb.tsv", "--corpus",
                    W / "alt.compound.train.txt", "--corpus-is-gold", "--dev",
                    W / "khpos.trainall.dev.txt", "--out", W / "lb.em.tsv", "--iterations", "3",
                    "--bin", bin_dir], check=True)


def tune(evaluate: pathlib.Path, dev: pathlib.Path, dictionary: pathlib.Path) -> float:
    out = run([evaluate, "--gold", dev, "--dict", dictionary, "--sweep", "unk-cost=4:20:1"])
    m = re.search(r"best unk-cost = ([0-9.]+)", out)
    return float(m.group(1))


def score(evaluate, gold, dictionary, algo, unk, extra=()) -> list:
    args = [evaluate, "--gold", gold, "--dict", dictionary, "--algo", algo, "--tsv", "row"]
    if unk is not None:
        args += ["--unk-cost", f"{unk:g}"]
    out = run(args + list(extra))
    return out.strip().splitlines()[-1].split("\t")[1:]


def predict(khseg, gold, dictionary, algo, unk, dest) -> pathlib.Path:
    raw = "".join("".join(l.split()) + "\n" for l in gold.read_text(encoding="utf-8").splitlines())
    args = [khseg, "--dict", dictionary, "--algo", algo, "--unk-cost", f"{unk:g}"]
    out = subprocess.run([str(a) for a in args], input=raw, capture_output=True, text=True,
                         encoding="utf-8", check=True).stdout
    dest.write_text(out, encoding="utf-8", newline="\n")
    return dest


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True, type=pathlib.Path)
    ap.add_argument("--skip-prepare", action="store_true")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "bench/results/accuracy.md")
    args = ap.parse_args()

    suffix = ".exe" if sys.platform == "win32" else ""
    khseg = args.bin / f"khseg{suffix}"
    evaluate = args.bin / f"khseg-eval{suffix}"
    if not args.skip_prepare:
        prepare(args.bin)

    khpos_dev, khpos_test = W / "khpos.trainall.dev.txt", W / "khpos.test.txt"
    setups = [
        # (test label, dev, test, dictionary label, dictionary, algorithms)
        ("khPOS open test", khpos_dev, khpos_test, "khPOS train (in-domain)", W / "khpos.train.tsv",
         ["viterbi", "fmm", "bmm", "bimm"]),
        ("khPOS open test", khpos_dev, khpos_test, "ICU list, no counts", W / "icu.tsv", ["viterbi", "fmm"]),
        ("khPOS open test", khpos_dev, khpos_test, "open lists + SEALang/Bible counts", W / "lb.tsv",
         ["viterbi", "fmm"]),
        ("khPOS open test", khpos_dev, khpos_test, "open lists + EM counts from ALT text", W / "lb.em.tsv",
         ["viterbi"]),
        ("ALT test (compound)", W / "alt.compound.dev.txt", W / "alt.compound.test.txt",
         "ALT train (in-domain)", W / "alt.compound.train.tsv", ["viterbi", "fmm"]),
        ("ALT test (compound)", W / "alt.compound.dev.txt", W / "alt.compound.test.txt",
         "khPOS train (other corpus)", W / "khpos.train.tsv", ["viterbi"]),
        ("ALT test (atom)", W / "alt.atom.dev.txt", W / "alt.atom.test.txt",
         "ALT train (in-domain)", W / "alt.atom.train.tsv", ["viterbi", "fmm"]),
    ]

    rows = []
    for test_label, dev, test, dict_label, dictionary, algos in setups:
        unk = tune(evaluate, dev, dictionary)
        for algo in algos:
            r = score(evaluate, test, dictionary, algo, unk)
            rows.append((test_label, dict_label, algo, unk, r))
            print(test_label, dict_label, algo, unk, r[2], file=sys.stderr)
        if dictionary == W / "khpos.train.tsv" and test == khpos_test:
            r = score(evaluate, test, dictionary, "viterbi", None)
            rows.append((test_label, dict_label, "viterbi, default unk cost", None, r))

    # Paired bootstrap: Viterbi against forward maximal matching, in-domain khPOS.
    unk = tune(evaluate, khpos_dev, W / "khpos.train.tsv")
    a = predict(khseg, khpos_test, W / "khpos.train.tsv", "viterbi", unk, W / "pred.viterbi.txt")
    b = predict(khseg, khpos_test, W / "khpos.train.tsv", "fmm", unk, W / "pred.fmm.txt")
    cmp_out = run([evaluate, "--gold", khpos_test, "--pred", a, "--compare", b, "--dict",
                   W / "khpos.train.tsv"])
    diff_line = next(l.strip() for l in cmp_out.splitlines() if l.strip().startswith("difference"))

    commit = run(["git", "-C", ROOT, "describe", "--always", "--dirty"]).strip()
    lines = [
        "# Accuracy",
        "",
        f"Generated by `scripts/experiments.py` on {datetime.date.today().isoformat()} at commit {commit}.",
        "Word-level scores in percent. 95% intervals come from 1000 bootstrap resamples of test",
        "sentences. The unknown-cluster cost was chosen on the matching dev set (grid 4 to 20).",
        "OOV counts only Khmer gold words that are missing from the dictionary.",
        "",
        "| Test set | Dictionary | Algorithm | Unk cost | P | R | F1 (95% CI) | Boundary F1 | Exact | OOV rate | OOV recall |",
        "|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for test_label, dict_label, algo, unk, r in rows:
        p, rec, f1, lo, hi, bf1, exact, oov_rate, oov_rec = r
        unk_s = f"{unk:g}" if unk is not None else "default"
        lines.append(f"| {test_label} | {dict_label} | {algo} | {unk_s} | {p} | {rec} | "
                     f"**{f1}** ({lo} to {hi}) | {bf1} | {exact} | {oov_rate} | {oov_rec} |")
    lines += ["", "Viterbi against forward maximal matching on the khPOS open test (in-domain dictionary),",
              f"paired bootstrap: {diff_line}", ""]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
