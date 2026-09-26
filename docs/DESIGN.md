# khseg: Khmer Word Segmenter, Design and Implementation Plan

Status: design, pre-implementation · 2026-09-27

---

## 1. Goals and non-goals

**Goals**

- A C++20 library (`khseg::khseg`) with zero runtime dependencies, plus CLI tools: `khseg`, `khseg-eval`, `khseg-bench`, `khseg-dict`.
- Explicit, documented Khmer Character Cluster (KCC) rules. A word boundary may fall only on a KCC boundary.
- Three segmentation algorithms behind one API: forward maximal matching (FMM), backward maximal matching (BMM), and unigram Viterbi, which is the default.
- Lossless handling of the input: every input byte belongs to exactly one token. Numbers, Latin text, punctuation and whitespace become their own tokens.
- Reproducible evaluation (P/R/F1 with confidence intervals) and throughput benchmarks, suitable for a write-up.
- Python bindings (stretch).

**Non-goals (v1)**

- Neural or CRF models. The design leaves a seam for them (§6.6), and they can be used as comparison baselines.
- POS tagging, spell correction, transliteration.
- Legacy non-Unicode Khmer font encodings (Limon and similar). These are detected and warned about, never converted.

---

## 2. Architecture

```
bytes ──► UTF-8 decode ──► [normalize] ──► pre-tokenize (script runs)
                                              │
               ┌──────────────────────────────┼─────────────────────┐
               ▼                              ▼                     ▼
          Khmer run                   number / latin / punct    whitespace/ZWSP
               │                        (emitted as-is)          (boundary hints)
               ▼
        KCC split (cluster boundaries)
               ▼
        lattice over clusters  ◄── Dictionary (trie + costs)
               ▼
        FMM | BMM | BiMM | Viterbi
               ▼
        merge adjacent unknown clusters
               ▼
   std::vector<Token> (spans, not strings) ──► formatter (space | zwsp | json | clusters)
```

Design rules:

- **Spans, not strings.** Every stage produces offsets into the decoded buffer. Formatters copy bytes once at the end. The same data gives code-point and byte offsets without extra work.
- **Immutable model, mutable workspace.** `Dictionary` and `Segmenter` are immutable after construction, so they are thread-safe. Per-call scratch buffers live in a `Workspace` that callers can reuse, which means no allocations in steady state.
- **Each stage is a free function or a small class with its own tests.** The CLI and the Python bindings are thin shells around the same API.

### 2.1 Repository layout

```
KhmerWordSegmenter/
├── CMakeLists.txt, CMakePresets.json, LICENSE, README.md
├── cmake/                      # khsegConfig.cmake.in, warnings, sanitizers
├── include/khseg/
│   ├── khseg.hpp               # umbrella header
│   ├── utf8.hpp                # decode/encode, byte-offset map
│   ├── charclass.hpp           # Khmer code-point classes (table-driven)
│   ├── normalize.hpp           # optional reordering normalizer
│   ├── cluster.hpp             # KCC splitter + strict validator
│   ├── pretokenize.hpp         # script runs → coarse tokens
│   ├── dictionary.hpp          # TSV/binary load, costs
│   ├── trie.hpp                # Trie interface; RefTrie, DoubleArrayTrie
│   ├── segmenter.hpp           # Segmenter, Options, Workspace, Algorithm
│   ├── token.hpp               # Token, TokenType
│   ├── format.hpp              # space/zwsp/json/clusters writers
│   └── eval.hpp                # metric computation (reused by khseg-eval + tests)
├── src/                        # one .cpp per header
├── tools/
│   ├── khseg/main.cpp
│   ├── khseg-eval/main.cpp
│   ├── khseg-bench/main.cpp
│   └── khseg-dict/main.cpp     # build | stats | check
├── tests/
│   ├── unit/                   # GoogleTest: utf8, clusters, pretok, dict, trie, seg, format, eval
│   ├── cli/                    # CTest golden-file tests for the binaries
│   └── fuzz/                   # libFuzzer targets (CI, clang only)
├── data/
│   ├── sample/                 # tiny, self-authored, license-clean
│   │   ├── dict.tsv
│   │   ├── gold.txt
│   │   └── raw.txt
│   └── README.md               # where to get real data + licenses
├── scripts/                    # Python: fetch_data.py, convert_khpos.py, convert_alt.py,
│                               #         build_freq.py, split.py
├── python/                     # pybind11 module, pyproject.toml (scikit-build-core), tests
├── bench/results/              # committed benchmark/eval result tables (CSV + md)
└── docs/
    ├── DESIGN.md               # this file
    ├── clusters.md             # KCC rules, character tables, examples
    ├── algorithm.md            # FMM/BMM/Viterbi, cost model, unknown handling
    └── data.md                 # data pipeline + licensing detail
```

---

## 3. Stage-by-stage design

### 3.1 UTF-8 decoding (`utf8.hpp`)

- Hand-written strict decoder, either a DFA in the style of Höhrmann's decoder or a straightforward branchy one; the branchy version is simpler to verify, so write it first. It rejects overlong forms, surrogates (U+D800–DFFF), values above U+10FFFF, and truncated sequences.
- **Error policy** (`Options::invalid_utf8`):
  - `replace` (default): each maximal invalid subpart becomes U+FFFD, following the WHATWG/Unicode "maximal subpart" practice. The token keeps its original byte span.
  - `error`: throw `khseg::DecodeError{byte_offset}`.
- Output is a `std::u32string` (in the workspace) plus a parallel `std::vector<uint32_t> byte_offset` of size n+1. That makes code-point↔byte conversion O(1) for JSON output and the Python bindings.
- A leading UTF-8 BOM is skipped when it appears at the very start of the stream, and only there.

### 3.2 Character classes (`charclass.hpp`)

A single 128-entry `constexpr` table for U+1780–U+17FF, plus a few special cases. Every range in the table is checked against `UnicodeData.txt` in a unit test, so the table cannot drift silently.

| Class | Code points | Notes |
|---|---|---|
| `Cons` | U+1780–U+17A2 | consonants (35 code points) |
| `IndV` | U+17A3–U+17B3 | independent vowels (U+17A3/A4 deprecated; accept them anyway) |
| `Inherent` | U+17B4, U+17B5 | invisible inherent vowels, which should not appear in text. The normalizer strips them; otherwise they attach as marks |
| `DepV` | U+17B6–U+17C5 | dependent vowels |
| `Sign` | U+17C6–U+17C8 | nikahit, reahmuk, yuukaleapintu |
| `Shifter` | U+17C9, U+17CA | register shifters (muusikatoan, triisap) |
| `Robat` | U+17CC | robat, a separate class because the validator needs its slot |
| `Diac` | U+17CB, U+17CD–U+17D1, U+17D3, U+17DD | bantoc, toandakhiat, kakabat, ahsda, samyok sannya, viriam, bathamasat, atthacan |
| `Coeng` | U+17D2 | subscript marker |
| `Punct` | U+17D4–U+17D6, U+17D8–U+17DA | ។ ៕ ៖ ៘ ៙ ៚ |
| `LekToo` | U+17D7 | ៗ repetition mark (own class, policy in §3.4) |
| `Currency` | U+17DB | ៛ riel |
| `LetterSym` | U+17DC | avakrahasanya, which acts as a base |
| `Digit` | U+17E0–U+17E9 | Khmer digits |
| `NumSym` | U+17F0–U+17F9, U+19E0–U+19FF | lunar-date numerals and Khmer symbols |
| `Joiner` | U+200C ZWNJ, U+200D ZWJ | cluster-internal |
| `Zwsp` | U+200B | boundary hint, never inside a token |

### 3.3 Khmer Character Clusters (`cluster.hpp`)

A KCC is the smallest unit that cannot be split without breaking rendering. It is an orthographic unit, not a syllable. In ក|ម្ពុ|ជា, the coda of the first syllable, ម, *begins* the second cluster. So clusters constrain where boundaries may fall, but they do not decide where boundaries fall. That is the dictionary's job.

**Two rule sets, used for different purposes.**

1. **Tolerant splitter** (used for segmentation). It never fails, which matters because real text contains mis-ordered marks.

   ```
   KCC      := Base Mark*
   Base     := Cons | IndV | LetterSym
   Mark     := Coeng (Cons | IndV)       -- subscript: the consonant does NOT start a cluster
             | Coeng                      -- dangling coeng at end of run: absorbed
             | DepV | Sign | Shifter | Diac | Inherent | Joiner
   Orphan   := Mark+                      -- marks with no base (run start / after non-Khmer)
   ```

   Operationally: **a cluster boundary lies before position i iff `cp[i]` is a Base and `cp[i-1]` is not Coeng.** A run of marks with no base becomes a degenerate cluster flagged `orphan`. It is still a token, so the output stays lossless.

2. **Strict validator** (used by `khseg-dict check`, for diagnostics, and to flag suspicious input). It follows the canonical order in The Unicode Standard §16.4 and UTC document L2/22-290 ("Khmer encoding structure"):

   ```
   Base [Robat] (Coeng Cons){0,2} [Coeng Ro]? [Shifter] [Joiner] [DepV] [Diac|Sign]*
   ```

   While implementing, check the exact slot order against L2/22-290. SIL's MIT-licensed `khmer-normalizer` implements the same structure and is a good cross-reference. Violations are reported with the code point offset and are never fatal.

**Worked examples** (these become table-driven tests; the sources use `\u` escapes so editors cannot re-normalize them):

| Text | Code points | Clusters |
|---|---|---|
| ខ្មែរ | 1781 17D2 1798 17C2 179A | `ខ្មែ` `រ` |
| ស្ត្រី | 179F 17D2 178F 17D2 179A 17B8 | `ស្ត្រី` (two subscripts) |
| កម្ពុជា | 1780 1798 17D2 1796 17BB 1787 17B6 | `ក` `ម្ពុ` `ជា` |
| ញ៉ាំ | 1789 17C9 17B6 17C6 | `ញ៉ាំ` (shifter + vowel + sign) |
| ◌ា at run start | 17B6 | orphan cluster |
| ក្ at end | 1780 17D2 | `ក្` (dangling coeng absorbed) |

`docs/clusters.md` holds the full tables and rationale and becomes the "cluster rules" section of the README.

### 3.4 Pre-tokenizer (`pretokenize.hpp`)

This stage runs over code points and emits coarse tokens. Only `Khmer` runs go on to the segmenter.

| TokenType | Rule |
|---|---|
| `Khmer` | maximal run of Base/Mark/Joiner classes. It is split into clusters and then words |
| `Number` | `[0-9០-៩]+` with internal `[.,:]` allowed only *between* digits (`១២,០០០.៥០`, `10:30`) |
| `Latin` | `[A-Za-z][A-Za-z0-9]*`, with internal `'`/`-` between letters |
| `Punct` | one token per punctuation code point (Khmer ។ ៕ ៖ ៘ ៙ ៚ plus Unicode P*). Runs of the same char (`...`, `!!`) are one token |
| `Symbol` | ៛, lunar numerals, U+19E0 block, currency/math symbols |
| `Space` | whitespace, including U+200B (flagged `zwsp`) |
| `Other` | anything else: other scripts, emoji, U+FFFD. Each item is a code point plus its following combining marks (Mn/Me) |

**Policies (options with defaults):**

- `zwsp_is_boundary = true`. Existing ZWSPs are author-provided break hints. They split Khmer runs and are never removed by the space and JSON formatters.
- `lektoo = separate`. The ៗ mark becomes its own `Punct` token. The alternative, `attach`, glues it to the preceding word. Pick the default that matches the gold corpus conventions; check khPOS and ALT during data conversion (§5.3).
- Khmer text uses spaces as phrase separators. Spaces are always hard boundaries, and no Khmer word spans a space.

### 3.5 Normalization (`normalize.hpp`, optional, default **on**)

Real Khmer text often has visually identical but differently ordered sequences, for example a vowel typed before the coeng: `ខែ្មរ` vs `ខ្មែរ`. Dictionary lookups fail on these.

- Implement a *reordering-only* normalizer within a cluster, following L2/22-290. It sorts marks into canonical slot order (stable sort by slot, with coeng+consonant pairs moved as a unit), strips U+17B4/B5, and collapses duplicate identical marks.
- It must be **length-preserving or offset-mapped**. Store a `normalized→original` offset map so that output offsets always refer to the *original* input. The simplest approach is to keep the reorder inside each cluster: the cluster's span is unchanged, so token boundaries (which lie on cluster boundaries) map back exactly. Only the internal order changes, and it is used only for lookup.
- Apply the same normalizer to dictionary entries at load time.
- Test it against SIL `khmer-normalizer` (MIT) outputs on a sample.

### 3.6 Dictionary (`dictionary.hpp`)

**TSV format v1:**

```
# khseg dictionary v1
# columns: word <TAB> count      (count optional)
ខ្មែរ	15234
កម្ពុជា	9876
ជា	412553
```

- UTF-8. A BOM is tolerated, and `\r\n` is tolerated. `#` starts a comment line; blank lines are skipped.
- `count` is a non-negative number. A missing count means "known word, unseen in the corpus" and gets the smoothing floor.
- Duplicates are summed, and the loader warns about them.
- Validation on load (warnings go to a report, never to stderr from the library): the entry must be entirely Khmer classes, must not start with a Mark, and must pass the strict cluster validator. The entry is normalized before insertion. Rejected entries are counted and listed by `khseg-dict check`.
- Optional header directive `# format: logprob` for sources that ship log-probabilities (khmerlbdict-style) instead of counts.

**Cost model** (unigram, additive smoothing):

```
P(w)    = (c(w) + α) / (N + α·V)          N = Σ counts, V = |dictionary|, α default 0.5
cost(w) = −ln P(w)                         stored as float per entry
```

**Trie interface:** `common_prefix_search(const char32_t* s, size_t n, Callback(len, entry_id))`.

1. **`RefTrie`** (milestone M3). Nodes hold a sorted `vector<pair<char32_t, uint32_t>>` of children. It is simple, obviously correct, and serves as the test oracle.
2. **`DoubleArrayTrie`** (milestone M6).
   - Alphabet remap: U+1780–U+17FF → 1..128, ZWNJ → 129, ZWJ → 130, terminator → 0. Any other code point means "no match", because dictionary words are Khmer-only. With a 131-symbol alphabet, `base`/`check` stay as `int32` arrays.
   - Built with the standard first-fit construction over sorted keys, breadth-first, with a free list.
   - Terminal nodes store the entry id in `base` of the terminator transition.
   - Property test: for every dictionary word and 100k random Khmer strings (fixed seed), DAT and RefTrie return identical prefix-search results.
3. **Reverse trie.** BMM needs a second trie over the reversed code-point sequence of each word. It is built lazily, only when BMM or BiMM is requested.

**Binary format** (`khseg-dict build dict.tsv -o dict.khd`) contains a magic string, a version, an endianness tag, a checksum, the DAT arrays, the costs, and a normalization-version stamp. It is loaded with `fread` into a single allocation, with mmap as a later option. Target: load in milliseconds instead of re-parsing the TSV. The load path checks the version and the checksum, and rejects mismatched files with a clear error.

### 3.7 Segmentation (`segmenter.hpp`)

Input: one Khmer run with clusters `c₀…c_{n−1}` and boundaries `b₀…b_n` (code-point offsets). A bit-vector `is_boundary[cp_offset]` lets a trie walk check "does this match end on a cluster boundary?" in O(1).

A dictionary match starting at cluster i is valid only if it **ends exactly at a boundary b_j**. Matches that end mid-cluster are discarded; this is how the KCC constraint is enforced.

**FMM:** at cluster i, take the longest valid match. If there is none, emit `c_i` as unknown. Advance.

**BMM:** mirror image using the reverse trie, scanning from `b_n` backwards.

**BiMM:** run both and pick using the usual heuristic: fewer tokens, then fewer unknowns, then fewer single-cluster words, then FMM.

**Viterbi (default):**

```
best[0] = 0;  best[1..n] = +∞
for i in 0..n-1 with best[i] < ∞:
    for each valid dictionary match w spanning clusters [i, j):
        relax(j, best[i] + cost(w), back = (i, w))
    relax(i+1, best[i] + C_unk, back = (i, UNK))          # 1-cluster unknown edge
backtrack from n; merge adjacent UNK edges into one KhmerUnknown token
```

- **Complexity:** O(n · L), where L is the longest dictionary word in code points. The trie walk stops early. Memory is O(n) inside the reusable workspace.
- **Unknown penalty `C_unk`.** A per-cluster cost, so a k-cluster OOV name costs k·C_unk. It must be larger than the cost of the rarest dictionary word; otherwise the segmenter prefers "unknown" over real words. It must also be small enough that an OOV name is not shredded into a chain of rare, spurious dictionary fragments.
  - Default: `C_unk = max_cost + δ` with δ = 1.0.
  - Tune it by grid search on a **dev** split: `khseg-eval --sweep unk-cost=…`.
  - Record the tuned value in the binary dictionary header so the CLI picks it up automatically.
- **Merging unknowns** (`merge_unknown = true`): consecutive UNK clusters become one token, so an OOV name is one token and not a string of clusters. This is the main defence for out-of-vocabulary names.
- **Tie-breaking** (determinism): compare `(cost, token_count)` lexicographically with an epsilon of 1e-9. Among remaining ties, prefer the longer last word. Store costs as `double` in the DP; the `float` in the trie is only for storage.
- **Ablation hooks** for the write-up:
  - (a) span-unknown edges `i→j` for j−i ≤ K with cost `C0 + (j−i)·C1`;
  - (b) a cluster-bigram character model for OOV plausibility.
  Both sit behind options, off by default.

### 3.8 Output formats (`format.hpp`)

| Mode | Behaviour | Lossless? |
|---|---|---|
| `space` (default) | Tokens joined by one space. Original whitespace tokens are dropped. One output line per input line. Matches the gold-file format | no |
| `zwsp` | Original text with U+200B **inserted** between two adjacent non-space tokens, where at least one of them is Khmer and no ZWSP is already there. **Idempotent**; this is tested | yes |
| `sep=STR` | Like `space` with a custom separator (`|` for debugging) | no |
| `json` | JSON Lines, one object per input line (see below). Offsets in code points by default, `--offsets byte` for bytes | yes |
| `clusters` | Clusters joined by `·`, for debugging KCC rules | no |

```json
{"line":1,"tokens":[
  {"text":"ខ្ញុំ","start":0,"end":5,"type":"word"},
  {"text":"ស្រលាញ់","start":5,"end":12,"type":"word"},
  {"text":"ប្រទេស","start":12,"end":18,"type":"word"},
  {"text":"កម្ពុជា","start":18,"end":25,"type":"word"}]}
```

`type` ∈ `word | unknown | number | latin | punct | symbol | space | other`. Code-point offsets are the default because they equal Python `str` indices. The JSON writer is hand-written (about 60 lines) and escapes `"`, `\\`, and control characters.

### 3.9 Public C++ API (sketch)

```cpp
namespace khseg {

enum class TokenType : uint8_t { Word, Unknown, Number, Latin, Punct, Symbol, Space, Other };
struct Token { uint32_t begin, end;            // code-point offsets into the original text
               uint32_t byte_begin, byte_end;
               TokenType type; uint32_t entry_id; };   // entry_id valid for Word

enum class Algorithm { Viterbi, Forward, Backward, Bidirectional };

struct Options {
  Algorithm algorithm = Algorithm::Viterbi;
  std::optional<double> unk_cost;      // nullopt → from dictionary header / default rule
  bool merge_unknown = true;
  bool normalize = true;
  bool zwsp_is_boundary = true;
  enum class LekToo { Separate, Attach } lektoo = LekToo::Separate;
  enum class InvalidUtf8 { Replace, Error } invalid_utf8 = InvalidUtf8::Replace;
};

class Dictionary {
 public:
  static Dictionary from_tsv(std::istream&, LoadReport* = nullptr);
  static Dictionary from_file(const std::filesystem::path&);   // .tsv or .khd by magic
  size_t size() const; double cost(uint32_t id) const; std::string word(uint32_t id) const;
};

class Workspace;   // opaque, reusable scratch; one per thread

class Segmenter {
 public:
  Segmenter(std::shared_ptr<const Dictionary>, Options = {});
  std::vector<Token> segment(std::string_view utf8) const;                  // convenience
  void segment(std::string_view utf8, Workspace&, std::vector<Token>& out) const;  // zero-alloc
};

std::vector<std::pair<uint32_t,uint32_t>> clusters(std::u32string_view);   // exposed for tests/tools
}
```

The library does no console I/O and does not print. Diagnostics go into `LoadReport`. Exceptions are used only for unrecoverable load errors and for `InvalidUtf8::Error`.

---

## 4. Tools

### 4.1 `khseg`

```
khseg [options] [FILE...]          # stdin if no FILE:   khseg < input.txt
  -d, --dict PATH        default: $KHSEG_DICT, else <exe>/../share/khseg/khmer.khd
  -a, --algo viterbi|fmm|bmm|bimm
  -f, --format space|zwsp|json|clusters      --zwsp and --json as shorthands
      --sep STR          --offsets cp|byte
      --unk-cost X       --no-merge-unknown   --no-normalize   --lektoo separate|attach
  -j, --threads N        (M8) order-preserving parallelism over line batches
      --version  --help
```

- Streams line by line with constant memory. Long lines are fine because the DP is O(n).
- **Windows specifics:**
  - Put stdin/stdout into binary mode with `_setmode(_fileno(stdin), _O_BINARY)` so CRLF translation and the code page cannot corrupt UTF-8.
  - Call `SetConsoleOutputCP(CP_UTF8)` only when stdout is a console.
  - Never use wide streams.
  - `\r\n` line endings are preserved on output.
- Argument parsing uses CLI11 (BSD-3, header-only, via FetchContent).
- Exit codes: 0 on success, 1 on usage error, 2 on I/O or dictionary error, 3 on invalid UTF-8 under `--strict-utf8`.

### 4.2 `khseg-eval`

```
khseg-eval --gold gold.txt (--pred pred.txt | --dict D [--algo ...])
           [--ignore-punct] [--dump-errors N] [--bootstrap 1000]
           [--sweep unk-cost=4:20:0.5] [--compare other_pred.txt]
```

- **Gold format:** one sentence per line, words separated by single spaces. Converters produce this format from khPOS and ALT (§5.3).
- **Method.** Remove the separators from the gold line to get the raw text. Segment the raw text, or read `--pred`. Map both segmentations to sets of `[start,end)` code-point spans over the same string, then:
  - `P = |pred ∩ gold| / |pred|`, `R = |pred ∩ gold| / |gold|`, `F1 = 2PR/(P+R)`, micro-averaged over the corpus.
  - If the concatenations differ (a normalization mismatch), the line is reported and excluded. The run fails if more than 0.1% of lines are excluded.
- **Also reported:**
  - boundary-level P/R/F1;
  - sentence exact-match rate;
  - OOV rate, OOV recall, and IV recall (relative to the dictionary);
  - a per-type breakdown.
- **Statistics:** a paired bootstrap over sentences gives a 95% CI for F1 and, with `--compare`, a p-value between two systems. This is what makes the results table publishable.
- `--dump-errors` prints gold vs. predicted with the differing spans marked. Use it to find a real sentence where FMM and Viterbi disagree for the README.
- `--sweep` runs the grid on dev and prints F1 per value; it is used to tune `C_unk` and α.

### 4.3 `khseg-bench`

- Input is loaded fully into memory. By default it is replicated to at least 100 MB.
- Reports, for each algorithm:
  - dictionary load time, TSV vs binary;
  - segmentation throughput in **MB/s of UTF-8 input**, as median and min of N≥5 runs after 1 warm-up;
  - peak RSS.
- End-to-end CLI throughput including I/O is measured separately with `khseg < big.txt > NUL`.
- Output is a CSV row per configuration, which goes into `bench/results/`. The machine, compiler, and flags are recorded in the header.
- **Optional baseline** (`-DKHSEG_BENCH_ICU=ON`): ICU `BreakIterator::createWordInstance(Locale("km"))`. It is dictionary-based and uses ICU's own Khmer word list, so it is a fair speed *and* accuracy baseline on the same test set.

### 4.4 `khseg-dict`

- `build in.tsv -o out.khd [--unk-cost X]` compiles the TSV to the binary format.
- `stats` prints the entry count, N, and the cost histogram, the longest words, and single-cluster words.
- `check` runs the strict validator and reports duplicates, non-Khmer entries, entries that normalize to another entry, and entries starting with a mark.

---

## 5. Data

### 5.1 What is openly available

| Resource | What | License | Use |
|---|---|---|---|
| ICU `khmerdict.txt` (unicode-org/icu, `icu4c/source/data/brkitr/dictionaries/`) | Khmer word list, **no frequencies** | Unicode License (permissive, attribution) | base word list |
| [sbbic/khmerlbdict](https://github.com/sbbic/khmerlbdict) | frequency-based word list built for ICU line breaking; includes place names, personal names, and spelling variants; log frequencies | MIT (repo), but one input is the SEALang frequency list | word list + prior frequencies. Confirm the upstream terms before *vendoring* a derived file; fetching at build time is safer |
| [khPOS](https://github.com/ye-kyaw-thu/khPOS) | 12,000 manually segmented + POS-tagged sentences with its own train/test splits | CC BY-NC-SA 4.0 | **gold evaluation**, counts for research builds |
| [ALT Khmer tokenized/POS](https://zenodo.org/records/3937914) | about 20,000 segmented sentences (NICT) | CC BY-NC-SA 4.0 | second gold set, with a different domain and conventions |
| Khmer Wikipedia dump (`kmwiki-latest-pages-articles`) | raw text | CC BY-SA 4.0 | unsupervised frequency counts |
| FLORES-200 `khm_Khmr` | about 2k professionally translated sentences | CC BY-SA 4.0 | out-of-domain sanity text |
| CC-100 / OSCAR / MADLAD-400 (km) | large web crawls | terms vary by dataset | counts only; never redistribute the text |
| [sillsdev/khmer-normalizer](https://github.com/sillsdev/khmer-normalizer) | reference normalizer | MIT | normalization cross-check |

Resources to avoid for distribution: `khmer-dictionary-44k` on Hugging Face (no license stated) and the Chuon Nath dictionary (copyrighted).

Both gold corpora are **non-commercial**. The code license (MIT or Apache-2.0) is unaffected, but any dictionary whose counts come from khPOS or ALT inherits NC-SA. Hence the two model builds:

- **`khmer-open`** (distributable): ICU list ∪ khmerlbdict, with counts from Wikipedia via EM (§5.2). It is attributed in `DATA_LICENSES.md` and released as a separate download, not committed to the repo.
- **`khmer-research`** (not distributed): additionally uses counts from the khPOS/ALT *train* splits. It is used for the paper's upper-bound numbers.

### 5.2 Building frequencies (`scripts/build_freq.py`, or a `khseg-dict train` subcommand later)

Most freely available word lists have no counts, and the large corpora are unsegmented. So bootstrap with hard EM (Viterbi training):

1. Normalize the raw corpus with the same normalizer as the library, invoked through the Python bindings or the CLI.
2. **Iteration 0:** give every dictionary word the same cost. Viterbi then minimizes the word count, which is roughly maximal matching. Segment the corpus and count dictionary-word tokens.
3. **Iteration k:** recompute costs from the counts with α-smoothing, resegment, and recount. After each iteration, evaluate on **dev** and stop at the first non-improvement, typically 2–4 iterations.
4. **Lexicon expansion:** unknown spans that recur at least T times, for example 20, are written to `candidates.tsv` for **human review**. They are never auto-added.
5. Write `word<TAB>count` sorted by count, with a provenance header recording sources, date, iteration, and α.

Risks: hard EM can lock in early mistakes, such as frequent wrong splits reinforcing themselves. The dev-set check at each iteration is the guard.

### 5.3 Converters and splits

- `convert_khpos.py`: khPOS `word/TAG` lines → gold format (strip tags). Keep its published train/test files.
- `convert_alt.py`: ALT tokenized format → gold format.
- While converting, **document each corpus's conventions**: how ៗ, numbers, names, and compounds are treated, and whether punctuation is attached. Differences explain cross-corpus F1 gaps and set the §3.4 defaults.
- `split.py`: deterministic split with a seeded hash of the sentence, 80/10/10, where the corpus has no official split. **Never tune on test.** The results table always states which split and which dictionary build it used.

### 5.4 Sample data committed in the repo (`data/sample/`)

- `dict.tsv`: about 200 common words with rough counts, **self-authored** (not copied from a licensed list), for example ខ្ញុំ, ជា, ទេ, ប្រទេស, កម្ពុជា, ខ្មែរ, and common particles.
- `gold.txt`: about 30 short self-authored sentences, hand-segmented.
- `raw.txt`: `gold.txt` with the separators removed.
- **Unit tests must not depend on the linguistic correctness of the sample.** They assert algorithm behaviour *given* a dictionary. A Khmer reader should still review the sample before release, because it becomes the README's first impression.

---

## 6. Algorithm write-up (README worked example)

The README walks one sentence through the pipeline. Draft:

**Input:** `ខ្ញុំស្រលាញ់ប្រទេសកម្ពុជា` ("I love the country of Cambodia"), 25 code points and 75 UTF-8 bytes.

**Step 1: Clusters (10).**

```
idx   0      1    2   3    4    5   6  7   8    9
     ខ្ញុំ | ស្រ | លា | ញ់ | ប្រ | ទេ | ស | ក | ម្ពុ | ជា
b:  0     5    8    10   12   15   17  18  19   23   25
```

**Step 2: Lattice.** These are the dictionary matches that end on a cluster boundary. The costs are *illustrative*, with C_unk = 12.

| span (clusters) | word | cost |
|---|---|---|
| [0,1) | ខ្ញុំ | 4.1 |
| [1,4) | ស្រលាញ់ | 7.9 |
| [4,7) | ប្រទេស | 6.2 |
| [5,6) | ទេ | 3.0 |
| [6,7) | ស | 7.5 |
| [7,8) | ក | 8.0 |
| [7,10) | កម្ពុជា | 6.8 |
| [9,10) | ជា | 3.2 |

**Step 3: Viterbi table.**

| i | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| best | 0 | 4.1 | 16.1ᵘ | 28.1ᵘ | **12.0** | 24.0ᵘ | 27.0 | **18.2** | 26.2 | 38.2ᵘ | **25.0** |
| from | – | 0 | 1 | 2 | 1 | 4 | 5 | 4 | 7 | 8 | 7 |

The competing path `…ប្រ(unk)+ទេ+ស…` reaches index 7 at a cost of 34.5 and loses to ប្រទេស at 18.2. Likewise ក + ម្ពុ(unk) + ជា costs 41.4, against 25.0 for កម្ពុជា.

**Step 4: Backtrack.** 10 ← 7 ← 4 ← 1 ← 0 gives `ខ្ញុំ ស្រលាញ់ ប្រទេស កម្ពុជា`.

**Step 5: OOV example.** Take a sentence containing a foreign name missing from the dictionary. The name's clusters take UNK edges, and merging turns them into one `unknown` token between known words.

**Step 6: FMM vs Viterbi.** Show a *real* sentence from the dev set where greedy matching fails; find it with `khseg-eval --dump-errors` comparing the two algorithms. Do not invent one.

### 6.6 Extension seam (future work)

A `Scorer` concept supplies edge costs, so a word-bigram model can drop in later. Bigram Viterbi runs over states of (position, previous word). A CRF or neural boundary scorer can also be added as lattice edge features. None of this is in v1, but keeping lattice construction separate from scoring costs almost nothing now.

---

## 7. Testing strategy

GoogleTest via FetchContent, pinned by tag. CTest runs everything.

| Area | Tests |
|---|---|
| utf8 | Valid 1–4 byte sequences. Overlong `C0 80` and `E0 80 80`. Surrogate `ED A0 80`. `F4 90 80 80` (above U+10FFFF). Truncation at end of input. Maximal-subpart replacement count. Byte-offset map. Round trip |
| charclass | Every table range checked against a vendored `UnicodeData.txt` excerpt |
| clusters | The §3.3 table. Orphan marks. Dangling coeng. Two subscripts. Robat. Shifters. ZWJ/ZWNJ. Khmer adjacent to Latin and digits. Strict-validator positives and negatives |
| normalize | Reorder cases (`ខែ្មរ` → `ខ្មែរ`). Idempotence. Offset map. Cross-check fixture from SIL khmer-normalizer |
| pretokenize | Numbers with separators (both digit sets). Latin with apostrophes. Punctuation runs. ZWSP splitting. ៗ policies. Emoji and other scripts |
| dictionary | Comments, BOM, CRLF, missing and invalid counts, duplicates summed, rejected entries reported, `logprob` header, cost formula |
| trie | RefTrie basics. **DAT ≡ RefTrie** property test (fixed seed, 100k random queries). Reverse trie. Binary save/load round trip, bad magic, bad checksum |
| segmentation | Tiny hand-built dictionaries that use real clusters as **opaque symbols** to force known outcomes: (a) a case where FMM ≠ BMM; (b) Viterbi picking the minimum-cost path over the longest-match path; (c) unknown merging; (d) an OOV span between known words; (e) empty input, whitespace only, a 1 MB single line; (f) tie-breaking determinism |
| invariants (every test input + fuzz) | Concatenated token spans == input bytes. Every Khmer token boundary is a cluster boundary. Output is deterministic. Offsets are monotone |
| format | Space, zwsp, json, clusters goldens. **ZWSP idempotence.** JSON escaping. cp vs byte offsets |
| eval | Hand-computed P/R/F1 on a 3-sentence fixture. Mismatch detection. Bootstrap with a fixed seed |
| CLI (golden files) | `khseg < data/sample/raw.txt` compared to an expected file for each format. Exit codes. Stdin vs FILE. CRLF input |
| regression | `khseg-eval` on the sample must give F1 ≥ a committed threshold. CI fails on a drop |
| fuzz (CI, clang) | libFuzzer targets: `utf8_decode`, `clusters`, `segment`, each asserting the invariants above |
| python | pytest: `segment`, `tokenize` offsets equal to `str` slicing, GIL release smoke test with threads |

---

## 8. Build, toolchain, CI

- **CMake ≥ 3.24, C++20.** Targets: `khseg` (static by default; `BUILD_SHARED_LIBS` supported with `GenerateExportHeader` for the Windows DLL), `khseg-cli` (output name `khseg`), `khseg-eval`, `khseg-bench`, `khseg-dict`, the tests, and `_khseg` (Python).
- **Options:** `KHSEG_BUILD_TESTS`, `KHSEG_BUILD_TOOLS`, `KHSEG_BUILD_PYTHON`, `KHSEG_BENCH_ICU`, `KHSEG_SANITIZE=address;undefined`, `KHSEG_WERROR`.
- **Install and export:** `find_package(khseg CONFIG)` provides `khseg::khseg`. The default dictionary installs to `share/khseg/`.
- **Warnings:** `-Wall -Wextra -Wpedantic -Wconversion` on GCC/Clang and `/W4 /utf-8` on MSVC. `/utf-8` is essential, because source files contain Khmer literals.
- **Local machine (checked):**
  - MinGW-w64 GCC 13.2 (UCRT), CMake 3.29, and Ninja are installed. MSVC and clang are not.
  - `CMakePresets.json` provides `mingw-debug` and `mingw-release` (Ninja), plus `msvc-*` and `linux-*` for CI.
  - C++20 support in GCC 13 is sufficient: `std::span` and `<format>`.
  - Avoid `char8_t` in the public API. Use `std::string_view` of UTF-8 bytes.
- **CI (GitHub Actions)** runs this matrix:
  - `ubuntu-latest` with gcc-13 and clang-18 (+ASan/UBSan, + a 60 s libFuzzer smoke run);
  - `windows-latest` with MSVC and with MinGW;
  - `macos-latest` with AppleClang.

  Every job runs the tests, the sample eval threshold, and the golden CLI tests. A separate job runs cibuildwheel for Python.
- **Style:** `.clang-format` and a `clang-tidy` config (modernize-*, bugprone-*, performance-*). Both are advisory in CI.

---

## 9. Python bindings (stretch)

```python
import khseg
seg = khseg.Segmenter("khmer.khd", algorithm="viterbi")
seg.segment("ខ្ញុំស្រលាញ់ប្រទេសកម្ពុជា")   # ['ខ្ញុំ', 'ស្រលាញ់', 'ប្រទេស', 'កម្ពុជា']
seg.tokenize(text)   # [Token(text, start, end, type)], start/end == Python str indices
khseg.clusters(text) # ['ខ្ញុំ', 'ស្រ', 'លា', 'ញ់', ...]
```

- pybind11, packaged with **scikit-build-core** through `pyproject.toml`, with type stubs (`.pyi`).
- `PyUnicode_AsUTF8AndSize` gives zero-copy UTF-8. The GIL is released during `segment`, and each thread gets its own `Workspace` (thread_local).
- **Windows caveat:** the installed CPython 3.13 is MSVC-built. Extension modules built with MinGW against MSVC CPython are fragile and unsupported. Either install **Visual Studio 2022 Build Tools** for the Python target, or build wheels only in CI with cibuildwheel. The C++ library and CLI are unaffected and stay on MinGW.

---

## 10. Performance plan

1. Get correctness first on RefTrie with the simple DP, then profile. Use `perf` on Linux CI or a sampling profiler on Windows.
2. Expected hot spots are the trie walk and the per-code-point boundary check. Fixes, in order:
   - switch to the DAT;
   - build the `is_boundary` bit-vector once per run;
   - reuse the workspace (no allocations per line);
   - use the ASCII fast path in the UTF-8 decoder and pre-tokenizer.
3. Set a throughput target *after* the first measurement. A reasonable goal to test against is ≥ 20 MB/s single-threaded for Viterbi in the Release build. Treat that as a hypothesis, not a promise.
4. Multi-threading (`-j`): batches of about 1 MB of lines, a bounded queue, `std::jthread` workers, and ordered output. Report the scaling curve for 1, 2, 4, and 8 threads.

---

## 11. Milestones

Each milestone ends with green CI and an updated README section.

| # | Deliverable | Done when |
|---|---|---|
| **M0** | Scaffold: CMake, presets, GoogleTest, CLI11, CI matrix, empty library, `khseg --version` | builds on MinGW locally and on all CI runners |
| **M1** | UTF-8 decoder, charclass table, KCC tolerant splitter + strict validator, `docs/clusters.md` | cluster tests pass; `khseg --format clusters` works |
| **M2** | Pre-tokenizer, Token model, all four formatters, Windows binary-mode I/O | golden CLI tests pass for clusters/json; ZWSP idempotence test |
| **M3** | TSV loader, RefTrie, reverse trie, FMM/BMM/BiMM, `data/sample/` | segmentation unit tests pass; sample output reviewed |
| **M4** | Viterbi, unknown edges and merge, tie-breaking, invariants test harness | Viterbi ≥ BiMM F1 on sample; invariant tests on all inputs |
| **M5** | `khseg-eval` (P/R/F1, boundary, OOV, bootstrap, sweep, dump), data scripts, khPOS/ALT converters, frequency builder | first real results table on the khPOS test split; C_unk tuned on dev |
| **M6** | Double-array trie, binary `.khd` format, `khseg-dict`, `khseg-bench`, profiling pass | DAT ≡ RefTrie property test; MB/s table committed |
| **M7** | README: algorithm, cluster rules, worked example (§6), data/licensing, results and benchmarks | a reader can reproduce every number from documented commands |
| **M8** (stretch) | Normalizer, `-j` threads, ICU baseline, Python bindings + wheels | pytest green on CI wheels; ICU comparison in results |

The critical path is M0 → M1 → M3 → M4 → M5. The normalizer (M8) can move earlier if M5 shows many dictionary misses caused by mark ordering; measure that with a `khseg-dict check`-style scan of the corpus.

---

## 12. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Different gold corpora use different segmentation conventions | F1 swings across corpora; tuning to one hurts the other | Report per corpus; document conventions (§5.3); build research dictionaries per convention |
| Dictionary convention (ICU line-breaking) differs from gold convention | systematic precision loss | Weight dictionary counts from gold-train (research build); analyze errors by type |
| Encoding noise: mis-ordered marks, stray ZWSP inside words, legacy font text | lookup misses, spurious unknowns | Normalizer; `zwsp_is_boundary` option; legacy-encoding heuristic warning (Latin-1 punctuation density in "Khmer" text) |
| License contamination of distributed data | cannot publish the default model | Two builds (§5.1); fetch-don't-vendor; `DATA_LICENSES.md`; CI check that no NC data is in the release artifact |
| Hard-EM degeneracy | worse frequencies than uniform | Dev-set early stopping; compare against uniform-cost and khmerlbdict-prior baselines |
| Windows console/encoding quirks | garbled CLI output | Binary mode, UTF-8 console code page, golden tests on Windows CI |
| MinGW vs MSVC-built Python | bindings fail to import | MSVC or cibuildwheel for Python only (§9) |

---

## 13. Decisions taken (change any of these before M0)

1. **Test framework: GoogleTest.** It is ubiquitous and has good parametrized tests. Catch2 v3 would work equally well.
2. **Project license: Apache-2.0** for the code. Data is licensed separately and attributed.
3. **Default algorithm: Viterbi, unigram.** FMM, BMM, and BiMM are kept as baselines.
4. **Offsets: code points by default**, with bytes on request.
5. **Default dictionary: fetched and built by a script**, not committed. Only `data/sample/` is in the repo.
6. **Python bindings: stretch (M8)**, built with MSVC or cibuildwheel.

## 14. References

- The Unicode Standard, ch. 16.4 "Khmer"; UTC L2/22-290 "Khmer encoding structure".
- ICU break iteration, Khmer dictionary: `unicode-org/icu` `icu4c/source/data/brkitr/dictionaries/khmerdict.txt`.
- sbbic/khmerlbdict: https://github.com/sbbic/khmerlbdict
- khPOS: https://github.com/ye-kyaw-thu/khPOS
- ALT Khmer tokenized/POS: https://zenodo.org/records/3937914; Kaing et al., "Towards Tokenization and Part-of-Speech Tagging for Khmer: Data and Discussion", ACM TALLIP 2021.
- SIL khmer-normalizer: https://github.com/sillsdev/khmer-normalizer
- Resource index: https://github.com/seanghay/awesome-khmer-language
- Aoki, "An Efficient Method of Storing and Retrieving Dynamic Keys" (double-array trie), and darts-clone for comparison.
