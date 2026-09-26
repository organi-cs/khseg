# khseg

khseg splits Khmer text into words. Khmer is written without spaces between
words, so search, spell checking, line breaking and most NLP tools need this
step first.

It is a C++20 library with command line tools. It splits text into Khmer
character clusters, then picks the most probable sequence of dictionary words
over those clusters with a unigram model and Viterbi search. Numbers, Latin
text and punctuation become separate tokens. Evaluation and benchmark tools
are included, with results on two public corpora.

```console
$ echo "ខ្ញុំស្រលាញ់ប្រទេសកម្ពុជា" | khseg -d khmer.khd
ខ្ញុំ ស្រលាញ់ ប្រទេស កម្ពុជា
```

Contents: [Build](#build) ·
[Command line](#command-line) ·
[Library](#library) ·
[Python](#python) ·
[How it works](#how-it-works) ·
[Worked example](#worked-example) ·
[Data](#data) ·
[Results](#results) ·
[Tools](#tools) ·
[Limitations](#limitations)

## Build

Needs CMake 3.24 or newer and a C++20 compiler. Development and all results
here use MinGW-w64 GCC 13.2 on Windows; the CI workflow also builds with GCC
and Clang on Linux, MSVC 2022 and macOS. GoogleTest and CLI11 are downloaded
by CMake. The library itself has no dependencies.

```bash
cmake --preset mingw-release      # or linux-release, or msvc
cmake --build --preset mingw-release
ctest --preset mingw-release
```

The binaries end up in `build/<preset>/tools/`. `cmake --install` puts the
library, headers and a CMake package config in the install prefix, so other
projects can use `find_package(khseg)` and link `khseg::khseg`.

## Command line

```bash
khseg [options] [FILE...]      # reads standard input when no FILE is given
```

| Option | Meaning |
|---|---|
| `-d, --dict PATH` | dictionary, TSV or binary `.khd` (default: `$KHSEG_DICT`, then `<exe>/../share/khseg/khmer.khd` or `.tsv`) |
| `-a, --algo NAME` | `viterbi` (default), `fmm`, `bmm` or `bimm` |
| `-f, --format NAME` | `space` (default), `zwsp`, `json` or `clusters` |
| `--zwsp`, `--json` | short forms of `--format` |
| `--sep STR` | separator for the space format |
| `--offsets cp\|byte` | JSON offsets in code points (default) or UTF-8 bytes |
| `--unk-cost X` | cost of one unknown cluster (default: from the dictionary) |
| `--no-merge-unknown` | keep unknown clusters as separate tokens |
| `--lektoo separate\|attach` | keep ៗ as its own token (default) or attach it to the word before |
| `--strict-utf8` | fail on invalid UTF-8 instead of replacing it with U+FFFD |
| `-v, --verbose` | report dictionary entries that were rejected |

Output formats, one output line per input line:

- **space**: tokens separated by one space. Original spaces are dropped, so
  this cannot be turned back into the input. It is the format gold files use.
- **zwsp**: the input unchanged, with ZERO WIDTH SPACE (U+200B) inserted
  between two adjacent Khmer words. Nothing else changes, line endings
  included, and running it twice gives the same result. This is the format
  for line breaking.
- **json**: one JSON object per line with every token, spaces included:

  ```json
  {"line":1,"tokens":[{"text":"ខ្ញុំ","start":0,"end":5,"type":"word"},
    {"text":"ស្រលាញ់","start":5,"end":12,"type":"word"}, ...]}
  ```

  `type` is one of `word`, `unknown`, `khmer` (not segmented because no
  dictionary was loaded), `number`, `latin`, `punct`, `symbol`, `space`,
  `other`. Offsets in code points match Python string indices.
- **clusters**: tokens separated by spaces, clusters inside Khmer tokens
  joined by `·`. Useful for checking the cluster rules.

Exit codes: 0 success, 1 bad arguments, 2 file or dictionary error,
3 invalid UTF-8 with `--strict-utf8`.

Input is read in binary mode on Windows, so CRLF line endings and UTF-8
survive, and output to a console is switched to UTF-8.

## Library

```cpp
#include <khseg/khseg.hpp>

auto dict = std::make_shared<const khseg::Dictionary>(
    khseg::Dictionary::from_file("khmer.khd"));
khseg::Segmenter seg(dict);  // Viterbi, default options

for (const std::string& w : seg.words("ខ្ញុំស្រលាញ់ប្រទេសកម្ពុជា")) std::cout << w << '\n';

// Allocation-free form for hot loops: reuse one Workspace per thread.
khseg::Workspace ws;
std::vector<khseg::Token> tokens;
seg.segment(line, ws, tokens);
for (const auto& t : tokens) {
  // t.begin/t.end are code point offsets, t.byte_begin/t.byte_end byte
  // offsets, t.type the token type, t.entry the dictionary id for words.
}
```

`Dictionary` and `Segmenter` do not change after construction, so one
instance can be shared by any number of threads, each with its own
`Workspace`. The library never prints; loading problems go into a
`LoadReport`.

Headers: `utf8.hpp` (decoder), `charclass.hpp` and `cluster.hpp` (Khmer
clusters), `pretokenize.hpp`, `dictionary.hpp`, `trie.hpp`, `segmenter.hpp`,
`format.hpp` (output writers), `eval.hpp` (scoring).

## Python

```bash
pip install .          # builds the C++ core with CMake through scikit-build-core
```

```python
import khseg

seg = khseg.Segmenter("khmer.khd")           # or a khseg.Dictionary, or None
seg.segment("ខ្ញុំស្រលាញ់ប្រទេសកម្ពុជា")       # ['ខ្ញុំ', 'ស្រលាញ់', 'ប្រទេស', 'កម្ពុជា']

text = "ក្មេងៗ លេង"
for t in seg.tokenize(text):                 # Token(text, start, end, type)
    assert text[t.start:t.end] == t.text      # offsets are str indices

khseg.clusters("ខ្មែរ")                        # ['ខ្មែ', 'រ']
khseg.normalize("ខែ្មរ")                      # 'ខ្មែរ' (vowel had been typed before the subscript)
```

`Segmenter` takes the same options as the command line (`algorithm`,
`unknown_cost`, `merge_unknown`, `normalize`, `lektoo`). `segment` and
`tokenize` release the GIL, so several Python threads can segment at once.
Type stubs are included. On Windows, the module also builds with MinGW
against the python.org (MSVC) CPython; that is how it is tested here.

## How it works

```
UTF-8 bytes
  -> code points (invalid bytes become U+FFFD, byte offsets kept)
  -> pre-tokens: Khmer runs, numbers, Latin words, punctuation, symbols, spaces
  -> each Khmer run is split into clusters
  -> dictionary words that start and end on cluster boundaries form a lattice
  -> Viterbi picks the cheapest path; unknown clusters next to each other merge
  -> tokens (offsets only) -> output writer
```

### Clusters

A Khmer consonant carries subscript consonants (written after COENG U+17D2),
vowel signs and other marks. The whole stack is one Khmer Character Cluster,
and a word boundary can only fall between clusters. The splitting rule is
local and never fails:

> position i starts a new cluster unless the previous code point is COENG,
> or this code point is a combining mark and the previous one is Khmer.

So ខ្មែរ is ខ្មែ | រ and ស្ត្រី is one cluster. Clusters are units of
writing, not syllables: in កម្ពុជា (ក | ម្ពុ | ជា) the m that ends the first
syllable is the base of the second cluster. That is why clusters limit where
boundaries may go but a dictionary decides where they do go.

Marks with no consonant before them become their own "orphan" cluster, so no
input is ever lost. A separate validator checks each cluster against the
canonical mark order (base, robat, up to two subscripts, register shifter,
vowel, signs) and reports problems such as a vowel typed before a subscript.
It is used for dictionary checks and never changes segmentation. The full
tables and rules are in [docs/clusters.md](docs/clusters.md).

### Pre-tokenizing

Before segmentation the text is cut by character type. Only Khmer letters
reach the segmenter. Digits (ASCII or Khmer, with `.` `,` `:` between digits)
become one number, Latin letters one word, and each punctuation mark (។ ៕ ៖
and ASCII) its own token. Spaces and ZERO WIDTH SPACE are always boundaries.
The repetition mark ៗ is a separate token by default.

### Dictionary and costs

A dictionary is a TSV file of words and counts (format in
[data/README.md](data/README.md)). Each word gets a cost, the negative log of
its smoothed probability:

```
cost(w) = -ln( (count(w) + alpha) / (N + alpha * V) )
```

where N is the total count, V the number of words and alpha 0.5. Words are
stored in a double-array trie over a 130-symbol alphabet (the Khmer block plus
ZWJ and ZWNJ); a lookup step is two array reads. `khseg-dict build` writes a
binary `.khd` file that loads 10 to 25 times faster than the TSV.

### Maximal matching

Forward maximal matching (FMM) starts at the left, takes the longest
dictionary word that ends on a cluster boundary, and repeats. If no word
starts at a position, one cluster is taken as unknown. Backward matching (BMM)
does the same from the right with a trie of reversed words. Bidirectional
matching runs both and keeps the result with fewer tokens, then fewer
unknowns, then fewer one-cluster words, then the backward result. These are
the baselines.

### Viterbi

For a Khmer run with cluster boundaries b0 .. bn, let best[i] be the
cheapest way to segment the text up to bi. From every reachable boundary the
trie is walked forward, and each dictionary word that ends on a boundary
offers an edge with that word's cost. Every single cluster also offers an
"unknown" edge with a fixed cost C_unk. best[n] is the answer, and following
the back pointers gives the words.

The unknown cost has to sit in a narrow range. If it is lower than the cost of
real words, the segmenter prefers "unknown" over the dictionary. If it is much
higher, an unknown name gets cut into a chain of rare dictionary fragments
instead of being left as one piece. The default is the cost of the rarest
dictionary word plus 1; `khseg-eval --sweep unk-cost=...` finds a better value
on a dev set, and a dictionary file can store it (`# unknown-cost:`).

Adjacent unknown clusters are merged into one `unknown` token, so an
out-of-vocabulary name comes out whole when none of its parts are words.

Ties are broken so that output is deterministic: equal cost goes to the path
with fewer tokens, and after that to the path whose last word is longest.

The search is linear in the length of the text times the length of the
longest dictionary word, and it does not recurse.

## Worked example

Take ខ្ញុំស្រលាញ់ប្រទេសកម្ពុជា ("I love the country of Cambodia"): 25 code
points, 75 bytes.

**Clusters.** The text has 10 clusters:

```
index      0     1    2    3    4    5   6   7    8    9
         ខ្ញុំ | ស្រ | លា | ញ់ | ប្រ | ទេ | ស | ក | ម្ពុ | ជា
offset  0     5    8    10   12   15   17  18  19   23   25
```

**Lattice.** With a small dictionary and these made-up costs (C_unk = 12):

| Clusters | Word | Cost |
|---|---|---|
| 0 to 1 | ខ្ញុំ (I) | 4.1 |
| 1 to 4 | ស្រលាញ់ (love) | 7.9 |
| 4 to 7 | ប្រទេស (country) | 6.2 |
| 5 to 6 | ទេ (no, particle) | 3.0 |
| 6 to 7 | ស (white) | 7.5 |
| 7 to 8 | ក (neck) | 8.0 |
| 7 to 10 | កម្ពុជា (Cambodia) | 6.8 |
| 9 to 10 | ជា (to be) | 3.2 |

**Viterbi table.** best[i] and where it came from (u marks an unknown edge):

| i | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| best | 0 | 4.1 | 16.1 u | 28.1 u | **12.0** | 24.0 u | 27.0 | **18.2** | 26.2 | 38.2 u | **25.0** |
| from | | 0 | 1 | 2 | 1 | 4 | 5 | 4 | 7 | 8 | 7 |

Some of the choices along the way:

- best[4]: ស្រលាញ់ from boundary 1 costs 4.1 + 7.9 = 12.0. The other way,
  three unknown clusters, costs 40.1.
- best[7]: ប្រទេស from 4 costs 12.0 + 6.2 = 18.2. The alternative
  ប្រ (unknown) + ទេ + ស costs 24.0 + 3.0 + 7.5 = 34.5.
- best[10]: កម្ពុជា from 7 costs 18.2 + 6.8 = 25.0. The alternative
  ក + ម្ពុ (unknown) + ជា costs 26.2 + 12 + 3.2 = 41.4.

**Back pointers.** 10 ← 7 ← 4 ← 1 ← 0 gives

```
ខ្ញុំ ស្រលាញ់ ប្រទេស កម្ពុជា
```

The unit test `Viterbi.ReadmeWorkedExample` builds this exact dictionary and
checks every cell of the table.

**A real case where greedy matching fails.** From the khPOS dev set, with the
dictionary built from khPOS training data (real costs, C_unk = 8), the
phrase បន្តរហូតដល់ ("continue until") has clusters ប | ន្ត | រ | ហូ | ត | ដ | ល់
and these dictionary edges:

| Word | Cost |
|---|---|
| បន្ត (continue) | 6.44 |
| បន្តរ (a rarer spelling) | 11.21 |
| រហូត (until) | 8.95 |
| រហូតដល់ (until, reaching) | 8.31 |
| ហូត | 10.37 |
| ដល់ (reach, to) | 6.09 |

The dictionary also has បន, but it ends inside the cluster ន្ត, so it is not
a candidate.

Forward matching takes the longest word first, បន្តរ, and has to continue
with ហូត ដល់, which costs 27.67 in total. Viterbi finds បន្ត រហូតដល់ at
14.75, which matches the gold segmentation.

## Data

The repository contains only a small hand-written sample (`data/sample/`)
for tests. Real resources are downloaded by `scripts/fetch_data.py` into
`data/external/`, which git ignores.

| Resource | Contents | License |
|---|---|---|
| ICU `khmerdict.txt` | 81,000 words, no counts | Unicode License |
| [sbbic/khmerlbdict](https://github.com/sbbic/khmerlbdict) | word lists with counts (SEALang frequency list, Bible word counts, names, places) | MIT, but check the SEALang terms before shipping derived files |
| [khPOS](https://github.com/ye-kyaw-thu/khPOS) | 12,000 segmented and tagged sentences plus a 1,000 sentence open test set | CC BY-NC-SA 4.0 |
| [ALT Khmer](https://zenodo.org/records/3937914) | 20,000 segmented sentences (NICT) | CC BY-NC-SA 4.0 |

khPOS and ALT are non-commercial. Use them to evaluate, and do not ship a
dictionary whose counts come from them with a commercial product.

`scripts/build_dict.py` merges word lists and counts into a dictionary TSV.
`scripts/build_freq.py` estimates counts from unsegmented text when you have a
word list but no counts: it segments the text, counts the words chosen,
rebuilds the dictionary and repeats (hard EM), keeping the round with the best
dev score. It also writes frequent unknown strings to a file for a person to
review; it never adds words by itself.

## Results

Full table with confidence intervals: [bench/results/accuracy.md](bench/results/accuracy.md).
Everything there is produced by

```bash
python scripts/fetch_data.py
python scripts/experiments.py --bin build/mingw-release/tools
```

The unknown cost is tuned on a dev split and each test set is scored once.
Word-level F1, in percent:

| Test set | Dictionary | Viterbi | FMM |
|---|---|---|---|
| khPOS open test | khPOS training words and counts | **94.90** | 93.93 |
| khPOS open test | open word lists with SEALang/Bible counts | **79.21** | 75.60 |
| khPOS open test | open word lists, counts from EM on ALT text | 77.26 | |
| khPOS open test | ICU list only (no counts) | 76.48 | 76.44 |
| ALT test, atom level | ALT training words and counts | **93.79** | 89.76 |
| ALT test, compound level | ALT training words and counts | **83.02** | 78.62 |
| ALT test, compound level | khPOS training words (other corpus) | 73.27 | |

On khPOS, Viterbi beats forward matching by 0.97 points (95% CI 0.68 to
1.27, paired bootstrap, p < 0.001).

What the numbers say:

- With a dictionary that follows the test set's conventions, a unigram model
  gets about 95 F1. Most remaining errors are names and other unknown words.
- With open word lists the score drops to about 79. Most of the gap comes from
  disagreement about what a word is, not from search errors. The ICU and
  khmerlbdict lists contain many compounds (ប្រទេសកម្ពុជា, នៅក្នុង) that
  khPOS splits, and khPOS joins some things they split. khPOS also writes
  decimals as ១ . ០ while khseg keeps ១.០ as one number.
- Counts matter: without them (ICU list only) Viterbi is no better than
  forward matching. Starting from equal counts, hard EM on unsegmented ALT
  text raised dev F1 from 75.3 to 77.0 in two rounds; on the test set these
  EM counts give 77.26, against 79.21 with the SEALang/Bible counts.
- The two corpora disagree with each other: a khPOS dictionary scores 73 on
  ALT.

Speed, single thread, Ryzen 5 5600H, GCC 13
([details](bench/results/throughput.md)):

| | MB/s, 94k-word dictionary | MB/s, 7k-word dictionary |
|---|---|---|
| Viterbi | 48.9 | 88.1 |
| forward matching | 96.5 | 145.9 |

The command line tool processes a 50 MB file in 1.3 s including I/O. The
binary dictionary loads in 20 to 60 ms against about 520 ms for the TSV.

## Tools

**khseg-eval** scores a segmenter against a gold file (one sentence per line,
words separated by spaces). It reports word and boundary precision, recall
and F1, exact sentence matches, out-of-vocabulary rate and recall, and a
bootstrap 95% interval for F1.

```bash
khseg-eval --gold test.txt --dict khmer.khd                  # run khseg on the gold text
khseg-eval --gold test.txt --pred mine.txt --compare other.txt   # paired bootstrap test
khseg-eval --gold dev.txt --dict khmer.khd --sweep unk-cost=4:20:1
khseg-eval --gold dev.txt --dict khmer.khd --dump-errors 20
```

Words are compared as spans over the unsegmented text, so a predicted word
counts only when both of its ends are right. Bootstrap sampling does not use
`std::uniform_int_distribution`, so intervals are the same with every
compiler.

**khseg-dict** builds and inspects dictionaries:

```bash
khseg-dict build khmer.tsv -o khmer.khd [--unk-cost 8]
khseg-dict stats khmer.khd
khseg-dict check khmer.tsv     # every rejected or suspicious entry
```

**khseg-bench** measures in-memory throughput for each algorithm and appends
CSV rows:

```bash
khseg-bench -d khmer.khd -i corpus.txt --min-mb 100 --repeat 5 --csv results.csv
```

## Testing

`ctest` runs 134 tests:

- UTF-8 decoding, including every class of malformed input.
- The character table checked against UnicodeData.txt.
- Cluster splitting and validation.
- Pre-tokenizing, dictionary loading, and all four algorithms on small
  dictionaries built to force known outcomes.
- The worked example above.
- A property test that the double-array trie agrees with a simple reference
  trie on 100,000 random queries.
- Binary dictionary round trips and corruption checks.
- Golden-file tests of the command line tools.

An invariants suite runs every algorithm on edge cases, invalid UTF-8, a 1 MB
line and random strings. It checks that the tokens cover the input exactly,
that every boundary is a cluster boundary, that word tokens match their
dictionary entry, and that output is deterministic.

## Limitations

- The model is unigram. It cannot use context, so frequent short words
  sometimes win where a longer word was meant. A word bigram model would fit
  into the same search.
- Unknown names that contain real words are split around them. Unknown
  clusters only merge with each other, so a name like អេលីសាបិត comes out as
  អេ លី សាបិត because លី is in the dictionary. A cost for unknown spans of
  several clusters, or a character model for names, would help.
- There is no Unicode normalization yet. Text with marks typed in a
  non-standard order (ខែ្មរ for ខ្មែរ) will not match the dictionary. The
  cluster validator finds such text but does not fix it.
- No dictionary is shipped. You build one from the sources above, and the
  licenses decide what you can redistribute.
- Text in legacy (non-Unicode) Khmer fonts is not detected.

## Project layout

```
include/khseg/   public headers
src/             library
tools/           khseg, khseg-eval, khseg-dict, khseg-bench
tests/           unit tests (GoogleTest) and CLI golden tests
scripts/         data download, conversion, dictionary building, experiments
data/sample/     small hand-written dictionary and gold file
bench/results/   accuracy and throughput results
docs/            design notes and cluster rules
```

## License

Code: Apache-2.0 (see `LICENSE`). The sample data in `data/sample/` is under
the same license. Downloaded data keeps its own license; see
[data/README.md](data/README.md).
