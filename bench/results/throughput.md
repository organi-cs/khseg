# Throughput

Measured 2026-09-27 on an AMD Ryzen 5 5600H (6 cores), 16 GB RAM, Windows 11,
MinGW-w64 GCC 13.2.0, `mingw-release` preset (`-O3`, no `-march`). Single
thread. MB means 10^6 bytes of UTF-8 input.

Input: the unsegmented sentences of the ALT training split and all of khPOS
(8.4 MB, 28,022 lines), repeated to 100.6 MB and held in memory, so file I/O
is not included. Each figure is the median of 5 passes after one warm-up
pass. Raw rows are in `throughput.csv`.

Commands (`experiments.py` writes `data/work/bench.txt` and the dictionaries):

```bash
python scripts/experiments.py --bin build/mingw-release/tools
khseg-dict build data/work/lb.tsv -o data/work/lb.khd
khseg-bench -d data/work/lb.khd -i data/work/bench.txt --min-mb 100 --repeat 5
```

## Open word lists (94,060 words)

| Configuration | MB/s | Tokens/s |
|---|---|---|
| decode + pre-tokenize only | 379.8 | 4.57 M |
| Viterbi | 48.9 | 3.80 M |
| forward maximal matching | 96.5 | 7.20 M |
| backward maximal matching | 94.0 | 7.02 M |
| bidirectional maximal matching | 46.5 | 3.45 M |

## khPOS training vocabulary (6,877 words)

| Configuration | MB/s | Tokens/s |
|---|---|---|
| decode + pre-tokenize only | 411.0 | 4.95 M |
| Viterbi | 88.1 | 6.76 M |
| forward maximal matching | 145.9 | 11.20 M |
| backward maximal matching | 141.0 | 10.82 M |
| bidirectional maximal matching | 85.8 | 6.58 M |

Viterbi tries a dictionary walk from every cluster boundary, while maximal
matching only walks from the start of each chosen word, so Viterbi doing
about half the work rate is expected. A larger dictionary means longer trie
walks. Bidirectional matching runs both directions and so costs about as
much as Viterbi.

## Dictionary loading (94,060 words)

| Format | Time |
|---|---|
| TSV (parse, validate, build both tries) | about 520 ms |
| binary `.khd` (read, checksum, validate) | 20 to 60 ms |

The binary file is 13 MB, most of it the two double-array tries (7.5 MiB).

## End to end

`khseg -d lb.khd FILE > out` on a 50.4 MB file, including reading, writing
and process start: 1.26 s (40 MB/s) for space-separated output and 1.74 s
(29 MB/s) for JSON Lines.

## Threads (`-j`)

Same 50.4 MB file and 94k-word dictionary, space-separated output written
to a file, best of 3 runs, end to end:

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
