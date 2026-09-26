# Throughput

Measured 2026-09-27 on an AMD Ryzen 5 5600H (6 cores, 12 threads), 16 GB RAM,
Windows 11, MinGW-w64 GCC 13.2.0, `mingw-release` preset (`-O3`, no
`-march`). MB means 10^6 bytes of UTF-8 input. Normalization is on (the
default). Repeated runs on this laptop vary by about 10%.

Input: the unsegmented sentences of the ALT training split and all of khPOS
(8.4 MB, 28,022 lines), repeated to 100.6 MB and held in memory, so file I/O
is not included. Each figure is the median of 5 passes after one warm-up
pass, single thread. Raw rows are in `throughput.csv`.

Commands (`experiments.py` writes `data/work/bench.txt` and the dictionaries):

```bash
python scripts/experiments.py --bin build/mingw-release/tools
khseg-dict build data/work/lb.tsv -o data/work/lb.khd
khseg-bench -d data/work/lb.khd -i data/work/bench.txt --min-mb 100 --repeat 5
```

## Open word lists (94,060 words)

| Configuration | MB/s | Tokens/s |
|---|---|---|
| decode + pre-tokenize only | 353.5 | 4.26 M |
| Viterbi | 44.9 | 3.49 M |
| forward maximal matching | 78.0 | 5.81 M |
| backward maximal matching | 76.0 | 5.67 M |
| bidirectional maximal matching | 40.7 | 3.02 M |

## khPOS training vocabulary (6,877 words)

| Configuration | MB/s | Tokens/s |
|---|---|---|
| decode + pre-tokenize only | 325.3 | 3.92 M |
| Viterbi | 71.6 | 5.49 M |
| forward maximal matching | 106.8 | 8.20 M |
| backward maximal matching | 101.6 | 7.80 M |
| bidirectional maximal matching | 69.8 | 5.35 M |

Viterbi tries a dictionary walk from every cluster boundary, while maximal
matching only walks from the start of each chosen word, so Viterbi being
slower is expected. A larger dictionary means longer trie walks.
Bidirectional matching runs both directions and so costs about as much as
Viterbi.

Before normalization was added (commit 51ba654) the same benchmark gave
48.9 MB/s for Viterbi and 96.5 MB/s for forward matching with the large
dictionary. Checking every cluster for canonical order costs about 10% for
Viterbi and about 20% for maximal matching; `--no-normalize` turns it off.

## Dictionary loading (94,060 words)

| Format | Time |
|---|---|
| TSV (parse, validate, normalize, build both tries) | about 520 ms |
| binary `.khd` (read, checksum, validate) | 20 to 60 ms |

The binary file is 13 MB, most of it the two double-array tries (7.5 MiB).

## End to end

`khseg -d lb.khd FILE > out` on a 50.4 MB file (`bench.txt` six times),
including reading, writing and process start, best of 3: 1.53 s (33 MB/s)
for space-separated output and 1.87 s (27 MB/s) for JSON Lines.

## Threads (`-j`)

Same 50.4 MB file and dictionary, space-separated output written to a file,
best of 3 runs, end to end:

| Threads | Time | MB/s |
|---|---|---|
| 1 | 1.37 s | 36.9 |
| 2 | 0.79 s | 63.6 |
| 4 | 0.54 s | 93.5 |
| 6 | 0.47 s | 106.6 |
| 12 | 0.44 s | 114.8 |

Reading the input and writing the output stay on the main thread, which
limits the speed-up past about 4 threads. The output is byte for byte the
same for every thread count.
