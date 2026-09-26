# Khmer Character Clusters

Khmer is written without spaces between words. Each consonant can carry
subscript consonants, vowels and signs, which are stored after it in the text
and drawn above, below or around it. A word can only
end where one of these stacks ends. khseg calls such a stack a Khmer Character
Cluster (KCC) and never places a word boundary inside one.

Clusters do not line up with syllables. In កម្ពុជា (Cambodia) the
clusters are ក | ម្ពុ | ជា, while the syllables are kam | pu | cha: the final m
of the first syllable is written as the base of the second cluster. So
clusters only tell us where a boundary *may* go. The dictionary decides where
it *does* go.

The code is in `include/khseg/charclass.hpp` and `src/cluster.cpp`.

## Character classes

Every code point gets one class. Ranges were checked against UnicodeData.txt
(Unicode 16.0); `tests/unit/test_charclass.cpp` repeats that check against a
copy of the relevant lines in `tests/data/khmer_unicodedata.txt`.

| Class | Code points | Unicode names (examples) |
|---|---|---|
| Cons | U+1780..U+17A2 | KA ក .. QA អ |
| IndV | U+17A3..U+17B3 | independent vowels ឣ ឤ ឥ .. ឳ (U+17A3, U+17A4 are deprecated) |
| Inherent | U+17B4, U+17B5 | VOWEL INHERENT AQ / AA, invisible, should not be used |
| DepV | U+17B6..U+17C5 | dependent vowels ា ិ ី .. ៅ |
| Sign | U+17C6..U+17C8 | NIKAHIT ំ, REAHMUK ះ, YUUKALEAPINTU ៈ |
| Shifter | U+17C9, U+17CA | MUUSIKATOAN ៉, TRIISAP ៊ |
| Robat | U+17CC | ROBAT ៌ |
| Diac | U+17CB, U+17CD..U+17D1, U+17D3, U+17DD | BANTOC ់, TOANDAKHIAT ៍, KAKABAT ៎, AHSDA ៏, SAMYOK SANNYA ័, VIRIAM ៑, BATHAMASAT ៓, ATTHACAN ៝ |
| Coeng | U+17D2 | COENG ្ (the next consonant is written as a subscript) |
| Punct | U+17D4..U+17D6, U+17D8..U+17DA | KHAN ។, BARIYOOSAN ៕, CAMNUC PII KUUH ៖, BEYYAL ៘, PHNAEK MUAN ៙, KOOMUUT ៚ |
| LekToo | U+17D7 | LEK TOO ៗ (repeat the previous word) |
| Currency | U+17DB | RIEL ៛ |
| LetterSym | U+17DC | AVAKRAHASANYA ៜ, treated as a base |
| Digit | U+17E0..U+17E9 | ០ .. ៩ |
| NumSym | U+17F0..U+17F9, U+19E0..U+19FF | LEK ATTAK numerals, lunar date symbols |
| Joiner | U+200C, U+200D | ZWNJ, ZWJ |
| Zwsp | U+200B | ZERO WIDTH SPACE |
| Other | everything else | |

"Base" means Cons, IndV or LetterSym. "Mark" means Inherent, DepV, Sign,
Shifter, Robat, Diac or Coeng.

## Splitting rule

The splitter has to accept any text, including text typed in the wrong order,
so it uses one local rule instead of a full grammar. Position i starts a new
cluster unless one of these holds:

1. The previous code point is COENG and this one is a Khmer letter, mark or
   joiner. This is what makes a subscript consonant part of the cluster above
   it: in ក្រ the រ follows COENG, so it does not start a cluster.
2. This code point is a mark or joiner and the previous code point is a Khmer
   letter, mark or joiner. Marks attach to whatever Khmer cluster is open.

Everything else starts a cluster. In particular:

- A base that does not follow COENG starts a cluster.
- Digits, punctuation, spaces, ZWSP and non-Khmer characters are clusters of
  one code point each. (The pre-tokenizer groups them further; the clusterer
  does not care.)
- Marks with no Khmer letter before them (at the start of the text, or right
  after Latin text or a space) form an *orphan* cluster. Orphans still appear
  in the output so that no input is lost; `is_orphan()` detects them.
- A COENG at the very end of the text stays attached to its cluster.

The result is always a partition: joining the clusters gives back the input.
A test checks this on every Khmer code point plus joiners and stray COENGs.

## Canonical order and the validator

For diagnostics (`khseg-dict check`, and for flagging suspect input) khseg
also checks each Khmer cluster against this order:

```
Base [Robat] (Coeng C){0,2} [Shifter] [Joiner] [DepV] (Sign | Diac)*
```

where C is a consonant or independent vowel. This follows the structure
described in The Unicode Standard, section 16.4 (Khmer), and in the UTC
document L2/22-290 on Khmer encoding. The validator never changes the
text and never affects segmentation. It reports:

| Issue | Meaning | Example |
|---|---|---|
| orphan-mark | cluster starts with a mark | ា at the start of a line |
| dangling-coeng | COENG not followed by a consonant or independent vowel | ក + COENG + ា |
| too-many-subscripts | more than two COENG pairs | ក្ក្ក្ក |
| inherent-vowel | U+17B4 or U+17B5 present | |
| duplicate-mark | the same mark twice | ក + ា + ា |
| multiple-vowels | two different dependent vowels | ក + ា + ី |
| out-of-order | a mark from an earlier slot after a later one | ខែ្មរ (vowel typed before the subscript) |

The out-of-order case matters in practice. ខែ្មរ and ខ្មែរ look the same on
screen, but only the second one will match a dictionary entry. The optional
normalizer (planned for a later milestone) fixes these by reordering marks
inside a cluster.

## Examples

These are unit tests in `tests/unit/test_cluster.cpp`.

| Text | Code points | Clusters |
|---|---|---|
| ខ្មែរ | 1781 17D2 1798 17C2 179A | ខ្មែ, រ |
| ស្ត្រី | 179F 17D2 178F 17D2 179A 17B8 | ស្ត្រី (one cluster, two subscripts) |
| កម្ពុជា | 1780 1798 17D2 1796 17BB 1787 17B6 | ក, ម្ពុ, ជា |
| ញ៉ាំ | 1789 17C9 17B6 17C6 | ញ៉ាំ |
| ធម៌ | 1792 1798 17CC | ធ, ម៌ |
| ា at text start | 17B6 1780 | ា (orphan), ក |
| ក្ at text end | 1780 17D2 | ក្ |

The sentence ខ្ញុំស្រលាញ់ប្រទេសកម្ពុជា ("I love the country of Cambodia")
has 25 code points and 10 clusters:

```
ខ្ញុំ | ស្រ | លា | ញ់ | ប្រ | ទេ | ស | ក | ម្ពុ | ជា
boundaries: 0 5 8 10 12 15 17 18 19 23 25
```
