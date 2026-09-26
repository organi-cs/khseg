"""Khmer word segmentation.

    >>> import khseg
    >>> seg = khseg.Segmenter("khmer.khd")
    >>> seg.segment("ខ្ញុំស្រលាញ់ប្រទេសកម្ពុជា")
    ['ខ្ញុំ', 'ស្រលាញ់', 'ប្រទេស', 'កម្ពុជា']

Token offsets from ``Segmenter.tokenize`` are str indices, so
``text[t.start:t.end] == t.text``.
"""

from ._khseg import Dictionary, Segmenter, Token, __version__, clusters, normalize

__all__ = ["Dictionary", "Segmenter", "Token", "clusters", "normalize", "__version__"]
