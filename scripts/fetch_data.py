#!/usr/bin/env python3
"""Download Khmer word lists and corpora into data/external/.

Nothing downloaded here is committed to the repository. Several sources are
licensed CC BY-NC-SA; read the license notice printed for each one.

    python scripts/fetch_data.py                 # everything except Wikipedia and ICU binaries
    python scripts/fetch_data.py khpos alt       # only these
"""

import argparse
import pathlib
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXTERNAL = ROOT / "data" / "external"

KHPOS = "https://raw.githubusercontent.com/ye-kyaw-thu/khPOS/master/corpus-draft-ver-1.0/data/"
LBDICT = "https://raw.githubusercontent.com/sbbic/khmerlbdict/master/"
ICU = ("https://raw.githubusercontent.com/unicode-org/icu/main/icu4c/source/data/brkitr/"
       "dictionaries/khmerdict.txt")

SOURCES = {
    "khpos": {
        "license": "CC BY-NC-SA 4.0 (non-commercial). https://github.com/ye-kyaw-thu/khPOS",
        "files": {
            "train.all": KHPOS + "after-replace/train.all",
            "OPEN-TEST": KHPOS + "OPEN-TEST",
            "CLOSE-TEST": KHPOS + "CLOSE-TEST",
        },
    },
    "alt": {
        "license": "CC BY-NC-SA 4.0 (non-commercial). https://zenodo.org/records/3937914",
        "files": {"km-nova.zip": "https://zenodo.org/api/records/3937914/files/km-nova.zip/content"},
        "unzip": "km-nova.zip",
    },
    "icu": {
        "license": "Unicode License v3. https://www.unicode.org/license.txt",
        "files": {"khmerdict.txt": ICU},
    },
    "khmerlbdict": {
        "license": ("MIT for the repository; seafreq.txt comes from the SEALang frequency list, "
                    "check its terms before redistributing anything derived from it. "
                    "https://github.com/sbbic/khmerlbdict"),
        "files": {name: LBDICT + "src/" + name for name in
                  ["seafreq.txt", "villages.txt", "places.txt", "names.txt", "KHSV.txt", "KHOV.txt"]}
                 | {"LICENSE": LBDICT + "LICENSE"},
    },
    "icu-bin": {
        "license": ("Unicode License v3. Official ICU 78.3 Windows build, only needed for the "
                    "khseg-icu baseline on Windows; elsewhere install ICU from the system "
                    "package manager. https://github.com/unicode-org/icu/releases"),
        "files": {"icu4c-78.3-Win64-MSVC2022.zip":
                  "https://github.com/unicode-org/icu/releases/download/release-78.3/"
                  "icu4c-78.3-Win64-MSVC2022.zip"},
        "unzip": "icu4c-78.3-Win64-MSVC2022.zip",
        "optional": True,
    },
    "wiki": {
        "license": "CC BY-SA 4.0. https://dumps.wikimedia.org/kmwiki/",
        "files": {"kmwiki-latest-pages-articles.xml.bz2":
                  "https://dumps.wikimedia.org/kmwiki/latest/kmwiki-latest-pages-articles.xml.bz2"},
        "optional": True,
    },
}


def download(url: str, dest: pathlib.Path) -> None:
    if dest.exists() and dest.stat().st_size > 0:
        print(f"  have {dest.relative_to(ROOT)}")
        return
    print(f"  get  {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "khseg-fetch/1"})
    tmp = dest.with_suffix(dest.suffix + ".part")
    with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
        while chunk := r.read(1 << 20):
            f.write(chunk)
    tmp.replace(dest)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sources", nargs="*", help=f"any of: {', '.join(SOURCES)}")
    args = ap.parse_args()

    names = args.sources or [n for n, s in SOURCES.items() if not s.get("optional")]
    unknown = [n for n in names if n not in SOURCES]
    if unknown:
        print(f"unknown source(s): {', '.join(unknown)}", file=sys.stderr)
        return 1

    for name in names:
        src = SOURCES[name]
        out = EXTERNAL / name
        out.mkdir(parents=True, exist_ok=True)
        print(f"{name}: {src['license']}")
        for fname, url in src["files"].items():
            download(url, out / fname)
        if "unzip" in src:
            with zipfile.ZipFile(out / src["unzip"]) as z:
                z.extractall(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
