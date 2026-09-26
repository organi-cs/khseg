"""Tests for the Python bindings. Run with `python -m unittest` or pytest.

The package must be importable: either installed, or the build tree on
PYTHONPATH (<build>/python). KHSEG_SAMPLE_DICT can point at the sample
dictionary; by default it is found relative to this file.
"""

import os
import pathlib
import tempfile
import threading
import unittest

import khseg

ROOT = pathlib.Path(__file__).resolve().parents[2]
SAMPLE_DICT = os.environ.get("KHSEG_SAMPLE_DICT", str(ROOT / "data" / "sample" / "dict.tsv"))
SAMPLE_RAW = ROOT / "data" / "sample" / "raw.txt"
SAMPLE_GOLD = ROOT / "data" / "sample" / "gold.txt"

SENTENCE = "ខ្ញុំស្រលាញ់ប្រទេសកម្ពុជា"


class ClusterTests(unittest.TestCase):
    def test_clusters(self):
        self.assertEqual(khseg.clusters("ខ្មែរ"), ["ខ្មែ", "រ"])
        self.assertEqual(len(khseg.clusters(SENTENCE)), 10)
        self.assertEqual(khseg.clusters(""), [])

    def test_normalize(self):
        typed = "ខែ្មរ"  # vowel typed before the subscript
        self.assertEqual(khseg.normalize(typed), "ខ្មែរ")
        self.assertEqual(khseg.normalize("abc"), "abc")


class SegmenterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dictionary = khseg.Dictionary.load(SAMPLE_DICT)
        cls.seg = khseg.Segmenter(cls.dictionary)

    def test_sentence(self):
        self.assertEqual(self.seg.segment(SENTENCE), ["ខ្ញុំ", "ស្រលាញ់", "ប្រទេស", "កម្ពុជា"])

    def test_path_argument(self):
        self.assertEqual(khseg.Segmenter(SAMPLE_DICT).segment(SENTENCE), self.seg.segment(SENTENCE))

    def test_matches_sample_gold(self):
        raw = SAMPLE_RAW.read_text(encoding="utf-8").splitlines()
        gold = SAMPLE_GOLD.read_text(encoding="utf-8").splitlines()
        for r, g in zip(raw, gold):
            self.assertEqual(self.seg.segment(r), g.split())

    def test_token_offsets_are_str_indices(self):
        text = "Hello ខ្ញុំ 😀 ១២.៥ ក្មេងៗ"
        tokens = self.seg.tokenize(text)
        pos = 0
        for t in tokens:
            self.assertEqual(t.start, pos)
            self.assertEqual(text[t.start:t.end], t.text)
            pos = t.end
        self.assertEqual(pos, len(text))
        text_, start, end, kind = tokens[0]
        self.assertEqual((text_, start, end, kind), ("Hello", 0, 5, "latin"))

    def test_options(self):
        with self.assertRaises(ValueError):
            khseg.Segmenter(self.dictionary, algorithm="nope")
        fmm = khseg.Segmenter(self.dictionary, algorithm="fmm")
        self.assertEqual(fmm.segment(SENTENCE), self.seg.segment(SENTENCE))
        attach = khseg.Segmenter(self.dictionary, lektoo="attach")
        self.assertEqual(attach.segment("ក្មេងៗ"), ["ក្មេងៗ"])
        split = khseg.Segmenter(self.dictionary, merge_unknown=False)
        self.assertEqual(split.segment("សុខា"), ["សុ", "ខា"])
        self.assertEqual(self.seg.segment("សុខា"), ["សុខា"])
        self.assertEqual(khseg.Segmenter(self.dictionary, unknown_cost=3.0).unknown_cost, 3.0)

    def test_no_dictionary(self):
        self.assertEqual(khseg.Segmenter().segment(SENTENCE + " 12"), [SENTENCE, "12"])

    def test_dictionary_api(self):
        self.assertIn("ខ្មែរ", self.dictionary)
        self.assertIn("ខែ្មរ", self.dictionary)  # normalized lookup
        self.assertNotIn("សុខា", self.dictionary)
        self.assertGreater(len(self.dictionary), 100)
        self.assertIsNone(self.dictionary.cost("សុខា"))
        self.assertGreater(self.dictionary.cost("ខ្មែរ"), 0)

    def test_binary_round_trip(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "sample.khd")
            self.dictionary.save_binary(path)
            again = khseg.Dictionary.load(path)
            self.assertEqual(len(again), len(self.dictionary))
            self.assertEqual(khseg.Segmenter(again).segment(SENTENCE), self.seg.segment(SENTENCE))

    def test_threads(self):
        # segment() releases the GIL; every thread must get the same answers.
        lines = SAMPLE_RAW.read_text(encoding="utf-8").splitlines() * 20
        expected = [self.seg.segment(l) for l in lines]
        results = [None] * 4

        def work(k):
            results[k] = [self.seg.segment(l) for l in lines]

        threads = [threading.Thread(target=work, args=(k,)) for k in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        for r in results:
            self.assertEqual(r, expected)


if __name__ == "__main__":
    unittest.main()
