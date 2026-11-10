*November 6, 2026*

# corpus-sentence-aligner

A command-line tool, written in C++17, that takes a document and its translation and works out which sentences correspond. Typical inputs are English/French consent forms, questionnaires, reports or transcripts. The result is exported as a translation memory (TMX) that CAT tools can import, or as a spreadsheet (CSV, TSV or Excel) for checking by hand.

It implements the length-based statistical method of Gale & Church (1993) and adds a few refinements for real research documents. It reads Word `.docx` files directly and has **no third-party dependencies**: the ZIP reader, DEFLATE decompressor, XML scanner and Excel writer are all part of the code. It needs only standard C++17 and either CMake 3.10+ or plain `make`, all available before 2022. It is tested with GCC 11 and GCC 13.

```
$ csa-align tests/data/sample_en.txt tests/data/sample_fr.txt -o sample.tmx
sample_en.txt: 35 source / 34 target sentences (21 / 22 paragraphs), c=1.18286
  beads: 0-1=1 1-0=1 1-1=29 1-2=1 2-1=2  review=2
```

The summary on standard error reads: 35 English and 34 French sentences, French running about 18% longer, 29 one-to-one matches, two merges, one split, one sentence added and one omitted. Two beads are flagged for review.

---

## Why length-based alignment works

Translators rarely reorder sentences, and a long sentence is usually translated by a long sentence. Gale and Church showed that this alone, the *character length* of each sentence, is enough to align parallel texts with high accuracy, without a dictionary or any knowledge of either language.

The aligner treats a pair of documents as a sequence of **beads**. A bead groups *s* consecutive source sentences with *t* consecutive target sentences:

| Bead | Meaning | Prior P(bead) |
|---|---|---|
| 1-1 | one sentence translated by one sentence | 0.89 |
| 2-1 / 1-2 | two sentences merged into one, or one split into two | 0.089 |
| 1-0 / 0-1 | a sentence omitted from, or added to, the translation | 0.0099 |
| 2-2 | two sentences reorganised across two | 0.011 |
| 3-1 / 1-3 | heavier merges (paragraph level; optional at sentence level) | 0.005 |

For a bead with source length *l₁* and target length *l₂* (in characters), the length mismatch is scored as a z-score

    δ = (l₂ − c·l₁) / √(s² · l̄)

Here *c* is the expected number of target characters per source character, *s²* = 6.8 is the variance per character (Gale & Church's estimate), and *l̄* is the mean of the two lengths on a common scale. The cost of a bead is

    cost = −log P(bead) − log P(|δ|),   P(|δ|) = 2·(1 − Φ(|δ|))

which is the negative log probability of that bead type plus the two-tailed normal probability of a length mismatch at least that large. A dynamic-programming search then finds the sequence of beads with the lowest total cost covering both documents from start to finish.

## How the tool processes a document pair

```
 .docx / .txt ──► paragraphs ──► sentences ──► align paragraphs ──► align sentences ──► TMX / CSV / XLSX
                  (reader)       (EN/FR rules)  (Gale-Church)         inside each pair
```

1. **Read.** `.docx` files are unzipped and `word/document.xml` is scanned for paragraph text. The reader keeps tracked insertions but skips tracked deletions, ignores the duplicate copy Word stores for text boxes (`mc:Fallback`), skips field codes, turns tabs into spaces and manual line breaks into paragraph breaks, and drops superscript citation numbers (`insomnia²` becomes `insomnia`). Plain-text files may be UTF-8 or UTF-16 (with BOM) and use one paragraph per line by default.

2. **Split into sentences.** Rules are specific to English and to French:
   - Abbreviations that never end a sentence (`Dr.`, `e.g.`, `M.`, `Mme`, `p. ex.`, `c.-à-d.`, `et al.`).
   - Abbreviations that end one only before a capital (`etc.`, `a.m.`).
   - Abbreviations that are only abbreviations before a number (`No. 5`, `p. 12`).
   - Initials, list numbers (`1.`, `IV.`), decimals, URLs, and footnote numbers glued to a sentence end (`insomnia.2 Another…`).
   - French typography: a (no-break) space before `?` `!` `:` `;`, and guillemets `« … »`.

   You can add your own abbreviation lists (`--src-abbrev`, `--tgt-abbrev`).

3. **Align paragraphs, then sentences** (hierarchical mode, the default, as recommended in the original paper). Paragraphs are aligned first, using their lengths. Sentences are then aligned only *within* each matched paragraph group. Errors therefore cannot spread across a document, and headings and questionnaire items, which are often one-line paragraphs, anchor the alignment. `--mode flat` aligns all sentences in one pass instead.

4. **Export** the beads, with a cost and a *review* flag for anything unusual.

### Refinements beyond the 1993 paper

| Refinement | Why | Turn off with |
|---|---|---|
| **Robust estimate of *c*** | French text runs about 15–20% longer than English. *c* is first estimated from the documents, then re-fitted on confident 1-1 matches only, so an untranslated section cannot skew it. Very short documents fall back to a per-language-pair default. | `--c 1.0` (or any fixed value) |
| **Length model for unmatched sentences** | Classic Gale-Church scores an omitted sentence with the same match model, so the cost of leaving out a long passage explodes. An untranslated paragraph then gets forced into a bad 1-1 pair with an unrelated sentence. Here, unmatched units cost their length divided by the average unit length. | `--classic` |
| **Anchor cue** | Numbers, acronyms (BRCS, COVID), e-mail addresses and URLs that appear on both sides add a small penalty when they end up in different beads. Tokens found on only one side are ignored. Questionnaires and consent forms are full of such anchors. | `--no-anchors` |
| **Review flags** | Beads other than 1-1 / 2-1 / 1-2, or with an unusual length ratio (\|δ\| > 2.5), are marked for checking by a human. | `--review-delta Z` |
| **Search band** | On very large inputs (over 4 million DP cells) the search is limited to a band around the diagonal, which keeps alignment fast. | — |

`--classic` reproduces the original algorithm exactly: published priors, *s²* = 6.8, normal model for every bead, no anchors.

---

## Building

### With CMake (Linux, macOS, Windows)

```sh
cmake -S . -B build
cmake --build build --config Release
cd build && ctest
```

The executable is `build/csa-align` (on Windows with Visual Studio: `build\Release\csa-align.exe`).

### With make (GCC or Clang)

```sh
make            # builds bin/csa-align
make test       # builds and runs the test suite
```

On Windows, the same `make` works from MSYS2 / MinGW-w64.

---

## Usage

```sh
# English .docx → French .docx, translation memory for a CAT tool
csa-align consent_en.docx consent_fr.docx -o consent.tmx

# Review spreadsheet: wrapped text, frozen header, filters, flagged rows in yellow
csa-align consent_en.docx consent_fr.docx -o consent.xlsx

# CSV for French-locale Excel (semicolon separator)
csa-align module_en.docx module_fr.docx -o module.csv --csv-sep ";"

# French → English, Canadian language tags in the TMX
csa-align -s fr-CA -t en-CA source_fr.docx target_en.docx -o fr-en.tmx

# A whole corpus into one TMX: one "source<TAB>target" pair per line
csa-align --pairs corpus.tsv -o corpus.tmx
```

### Options

| Option | Effect |
|---|---|
| `-o FILE` | Output file. The format comes from its extension (`.tmx`, `.csv`, `.tsv`, `.xlsx`); the default is TMX to standard output. |
| `-f FMT` | Force the format: `tmx`, `csv`, `tsv`, `xlsx`. |
| `-s`, `-t` | Source and target language tags (default `en` and `fr`). These choose the segmentation rules and the TMX `xml:lang`. |
| `--pairs LIST` | Batch mode. Paths are relative to the list file; `#` starts a comment. Pairs that fail are reported and skipped. |
| `--mode flat` | Skip the paragraph stage. |
| `--presegmented` | Inputs already have one sentence per line. |
| `--blank-line-paragraphs` | Plain text: paragraphs are separated by blank lines, not line breaks. |
| `--keep-unaligned` | TMX: also write 1-0 / 0-1 units. CSV, TSV and XLSX always include them. |
| `--extended-beads` | Allow 3-1 / 1-3 at sentence level. |
| `--c`, `--s2` | Fix the model parameters. |
| `--classic`, `--no-anchors`, `--anchor-weight W` | Model variants (see above). |
| `--keep-superscript`, `--br-as-space` | `.docx` reading options. |
| `--src-abbrev FILE`, `--tgt-abbrev FILE` | Extra abbreviations, one per line. A trailing `*` means "ends a sentence before a capital". |
| `--dump-segments` | Print the numbered sentences of both sides and exit. |
| `--gold FILE` | Score the alignment against a reference (see below). |
| `--date STAMP` | Fixed TMX creation date, for reproducible output. |

Run `csa-align --help` for the complete list.

### Output columns (CSV / TSV / XLSX)

`document, bead, type, src_ids, tgt_ids, source, target, cost, delta, review`

Sentence ids are 1-based, numbered as `--dump-segments` shows them. A merged bead lists its ids as `4+5`. In the TMX, each `<tu>` carries the same information as `<prop>` elements (`x-alignment`, `x-src-ids`, `x-cost`, `x-review`, `x-document`).

### Checking accuracy against a reference alignment

A translator can correct the alignment by hand and use the result to measure accuracy:

1. Export a CSV: `csa-align a_en.docx a_fr.docx -o a.csv`
2. Fix the `src_ids` / `tgt_ids` columns where the tool was wrong (merge rows, split rows, empty a side for an omission). Save the file as CSV.
3. Score any setting against it:

```sh
$ csa-align --classic tests/data/sample_en.txt tests/data/sample_fr.txt --gold tests/data/sample_gold.tsv -o /dev/null
gold: links P=0.9459 R=1.0000 F1=0.9722 (35/37 test, 35 gold)
      beads P=0.9375 R=0.8824 (30 exact of 32 test, 34 gold)
```

**Link** scores count the individual (source sentence, target sentence) pairs. **Bead** scores count groups that match exactly, including omissions and additions. A plain two-column file (`1+2<TAB>3`) also works as a reference.

---

## Testing

The repository includes 58 unit and integration tests, using a small built-in harness with no downloads.

- **Reading:** UTF-8 and UTF-16 handling. The DEFLATE decoder is checked against streams produced by Python's `zlib` (stored, fixed-Huffman, dynamic-Huffman and RLE blocks). A hand-built `.docx` fixture covers tracked changes, superscripts, text boxes, fields, tabs and tables.
- **Sentence splitting:** English and French abbreviations and typography.
- **Alignment:** the Gale-Church cost function against hand-computed values. Every alignment must cover both documents in order (checked on random inputs). Banded and full search must agree.
- **Output:** escaping and quoting, and round-trips of the TMX, CSV and XLSX files.
- **End to end:** alignment against a hand-made reference.

`tests/data/sample_en.txt` / `sample_fr.txt` is an original bilingual newsletter written for this repository, with `sample_gold.tsv` as its reference alignment. It deliberately contains a 2-1 merge, a 1-2 split, a sentence added in French, a sentence omitted from French, a paragraph split in two, French no-break spaces and guillemets, an e-mail address and a phone number. Results on it:

| Setting | Link precision | Link recall | Exact beads |
|---|---|---|---|
| default (hierarchical) | 1.000 | 1.000 | 34 / 34 |
| `--mode flat` | 0.972 | 1.000 | 32 / 34 |
| `--classic` (original Gale-Church) | 0.946 | 1.000 | 30 / 34 |

This sample is small and was written by the same author as the code, so treat it as a regression test rather than a benchmark.

The code also has these checks:

- **Compilers:** it compiles without warnings (`-Wall -Wextra -Wpedantic`) under GCC 11 and GCC 13, and passes the tests under AddressSanitizer and UndefinedBehaviorSanitizer.
- **Corrupted input:** in a run of 300 randomly damaged `.docx` files, none crashed; each was either read or rejected with an error message.
- **Output validity:** the generated `.xlsx` opens in LibreOffice with the same text as the CSV export.
- **Speed:** 10,000 sentences per side align in about 4 seconds.

---

## Limitations

- **The only signal is length (plus anchors).** The method assumes the translation follows the source's order. Reordered sections, or a translation that adds or removes a lot, need checking; such beads are usually flagged for review.
- **A short sentence that is added or dropped** next to a translated one can still be absorbed into a 2-1 or 1-2 bead. This is inherent to length-based alignment.
- **Some formats are not read:** `.doc`, `.rtf`, `.odt` and `.pdf`. Save them as `.docx` or UTF-8 text first. Headers, footers, footnotes and comments in `.docx` files are not read.
- **Segmentation rules exist only for English and French.** Other languages fall back to generic rules (punctuation followed by a capital letter).
- **The default English→French ratio (`c` = 1.15)** applies only when a document is too short to estimate it. It has not yet been calibrated on a large corpus.
- **Windows:** file names are passed to the C++ standard library as narrow strings. Paths with characters outside the system code page may not open; rename the file or use a shorter path. The tool has not yet been tested on a Windows build.

## Project layout

```
include/csa/      public headers
src/              utf8, zip (+DEFLATE), document (.docx/.txt), segmenter,
                  gale_church (DP), aligner (hierarchy, anchors, c), export, evaluate, main
tests/            minitest harness, unit + integration tests, fixtures
tools/            make_test_vectors.py (regenerates DEFLATE vectors and fixture.docx)
```

## References

- Gale, W. A., & Church, K. W. (1993). A program for aligning sentences in bilingual corpora. *Computational Linguistics*, 19(1), 75–102.
- LISA OSCAR (2005). *TMX 1.4b Specification* (Translation Memory eXchange).
- Deutsch, P. (1996). *DEFLATE Compressed Data Format Specification version 1.3* (RFC 1951).
- ECMA-376. *Office Open XML File Formats* (WordprocessingML, SpreadsheetML).

## License

MIT. See [LICENSE](LICENSE).
