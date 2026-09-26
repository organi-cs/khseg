# Data

Only a small hand-written sample is kept in this repository. Real word lists
and corpora are downloaded by the scripts in `scripts/` into `data/external/`
(ignored by git), because several of them have licenses that do not allow
redistribution with the code.

## `sample/`

| File | Contents |
|---|---|
| `dict.tsv` | about 180 common words with rough counts |
| `gold.txt` | 32 short sentences, one per line, words separated by spaces |
| `raw.txt` | `gold.txt` with the spaces removed |

These were written for this project and are licensed with the code
(Apache-2.0). They exist so that tests and examples run without downloads.
The counts are guesses, not corpus counts, and the dictionary was built
around the gold sentences, so scores on the sample say nothing about real
accuracy. The sentences have not yet been checked by a native Khmer reader;
corrections are welcome.

`សុខា` (a personal name) is left out of the dictionary on purpose, to show
unknown-word handling.

## Dictionary format

UTF-8, one entry per line:

```
# comment
# format: count            (default) or logprob
# unknown-cost: 14.5       optional; cost of one unknown cluster
word<TAB>count
```

- A line starting with `#` is a comment. The two directives above are read
  from comments.
- The count may be missing (known word, never counted) or any non-negative
  number. With `format: logprob` the value is a natural log probability (at
  most 0).
- Words must be Khmer letters only and must start with a consonant or
  independent vowel. Other entries are rejected and reported.
- A repeated word has its counts added.
- A byte order mark and CRLF line endings are accepted.

Costs are `-ln((count + alpha) / (N + alpha * V))`, where N is the sum of all
counts, V the number of words and alpha 0.5 by default.

## Openly available sources

Check each license yourself before using or publishing anything built from
these.

| Resource | What it is | License |
|---|---|---|
| ICU `khmerdict.txt` (unicode-org/icu, `icu4c/source/data/brkitr/dictionaries/`) | Khmer word list, no frequencies | Unicode License |
| [sbbic/khmerlbdict](https://github.com/sbbic/khmerlbdict) | word list with frequencies, made for ICU line breaking | MIT (one input is the SEALang frequency list; check its terms before shipping derived files) |
| [khPOS](https://github.com/ye-kyaw-thu/khPOS) | 12,000 hand-segmented, POS-tagged sentences | CC BY-NC-SA 4.0 |
| [ALT Khmer tokenized data](https://zenodo.org/records/3937914) | about 20,000 segmented sentences | CC BY-NC-SA 4.0 |
| Khmer Wikipedia dump | raw text for counting | CC BY-SA 4.0 |
| FLORES-200 `khm_Khmr` | about 2,000 translated sentences | CC BY-SA 4.0 |

khPOS and ALT are non-commercial. Use them to evaluate, and keep any
dictionary whose counts come from them out of anything you distribute.

## Reproducing the accuracy numbers

```bash
python scripts/fetch_data.py
python scripts/experiments.py --bin build/mingw-release/tools
```

`fetch_data.py` downloads the sources above into `data/external/`.
`experiments.py` converts the corpora to gold format (`convert_khpos.py`,
`convert_alt.py`), splits off dev sets (`split.py`), builds dictionaries
(`build_dict.py`, and `build_freq.py` for counts estimated by hard EM),
tunes the unknown-cluster cost on dev, scores the test sets once and writes
`bench/results/accuracy.md`. Intermediate files go to `data/work/`.
