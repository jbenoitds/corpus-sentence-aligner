# corpus-sentence-aligner

`csa-align` takes a document and its translation and works out which sentences go together. I wrote it while I was translating study documents (consent forms, questionnaires, reports) between English and French as a research assistant: I wanted our old translations as a translation memory (TMX) that a CAT tool could reuse, and a spreadsheet I could check by hand.

It uses the length-based method of Gale & Church (1993), with a few changes that helped on real research documents. It reads `.docx` directly and has no third-party dependencies, so the ZIP reader, the DEFLATE decompressor, the XML scanning and the Excel writer are in the code. You only need a C++17 compiler and CMake (or `make`).

```
$ csa-align tests/data/sample_en.txt tests/data/sample_fr.txt -o sample.tmx
sample_en.txt: 35 source / 34 target sentences (21 / 22 paragraphs), c=1.18286
  beads: 0-1=1 1-0=1 1-1=29 1-2=1 2-1=2  review=2
```

So: 35 English and 34 French sentences, the French about 18% longer, 29 one-to-one matches, two merges, one split, one sentence added and one left out. Two groups are flagged for a human to look at.

## Project history

I wrote the first version in November 2019, on my own and without any AI tool. Like most of my projects, it then lived on my laptop without version control, so the history here starts with a single commit from November 2026, when I published it. I rewrote this README with help from Claude, an AI assistant; the code itself is mine.

## Building

```sh
cmake -S . -B build
cmake --build build --config Release
cd build && ctest
```

or, with GCC or Clang (also MSYS2/MinGW on Windows):

```sh
make            # bin/csa-align
make test
```

## Usage

```sh
# English .docx -> French .docx, translation memory for a CAT tool
csa-align consent_en.docx consent_fr.docx -o consent.tmx

# Spreadsheet for review (flagged rows in yellow)
csa-align consent_en.docx consent_fr.docx -o consent.xlsx

# CSV for a French-locale Excel
csa-align module_en.docx module_fr.docx -o module.csv --csv-sep ";"

# French -> English with Canadian language tags
csa-align -s fr-CA -t en-CA source_fr.docx target_en.docx -o fr-en.tmx

# Many pairs at once: one "source<TAB>target" per line
csa-align --pairs corpus.tsv -o corpus.tmx
```

Useful options (`csa-align --help` lists them all):

```
-o FILE                  output; format from the extension (.tmx .csv .tsv .xlsx), TMX to stdout by default
-s, -t                   source / target language (default en, fr)
--pairs LIST             batch mode; failed pairs are reported and skipped
--mode flat              skip the paragraph stage
--presegmented           one sentence per line already
--keep-unaligned         also write 1-0 / 0-1 units in the TMX
--classic                original Gale-Church, none of my changes
--src-abbrev, --tgt-abbrev FILE   extra abbreviations
--dump-segments          print the numbered sentences and stop
--gold FILE              score against a corrected alignment
```

CSV/TSV/XLSX columns: `document, bead, type, src_ids, tgt_ids, source, target, cost, delta, review`. Merged sentences show up as `4+5`.

## How it works

Translators rarely reorder sentences, and a long sentence usually becomes a long sentence. Gale and Church showed that sentence length in characters is enough to align most texts, with no dictionary.

The documents are cut into "beads": groups of *s* source sentences matched to *t* target sentences (1-1, 2-1, 1-2, 1-0, 0-1, 2-2). Each bead gets a cost from how unusual its type is and how far the length ratio is from the expected one:

    δ = (l₂ − c·l₁) / √(s² · l̄)
    cost = −log P(bead) − log P(|δ|)

and dynamic programming finds the cheapest sequence of beads from start to end.

The steps:

1. **Read** the text. For `.docx` this means unzipping it and scanning `word/document.xml`. Tracked insertions are kept and deletions skipped, text boxes are not read twice, and superscript citation numbers are dropped.
2. **Split into sentences** with English and French rules: abbreviations (`Dr.`, `p. ex.`, `c.-à-d.`, `et al.`), numbered lists, decimals, URLs, French spacing before `? ! : ;` and « guillemets ».
3. **Align paragraphs first, then sentences** inside each paragraph pair, as the paper recommends. A mistake can't spread through the whole document, and short headings and questionnaire items act as anchors.
4. **Export**, with a cost and a review flag on each bead.

What I changed from the 1993 paper, because our documents broke the original:

- **`c` (length ratio) is estimated from the documents**, then re-fitted on confident 1-1 matches, so an untranslated section doesn't skew it. Turn off with `--c 1.0`.
- **Left-out sentences have their own cost.** With the original model, a long untranslated paragraph got forced into a bad 1-1 pair. Turn off with `--classic`.
- **Anchors:** numbers, acronyms, e-mails and URLs found on both sides add a small penalty when they end up in different beads. Questionnaires are full of them. Turn off with `--no-anchors`.
- **Review flags** on unusual beads (|δ| > 2.5, or anything other than 1-1/2-1/1-2).
- On very large files the search stays in a band around the diagonal so it stays fast.

## Checking the result

Export a CSV, fix the `src_ids`/`tgt_ids` columns where the tool was wrong, and score any setting against your corrected file:

```sh
$ csa-align --classic tests/data/sample_en.txt tests/data/sample_fr.txt --gold tests/data/sample_gold.tsv -o /dev/null
gold: links P=0.9459 R=1.0000 F1=0.9722 (35/37 test, 35 gold)
      beads P=0.9375 R=0.8824 (30 exact of 32 test, 34 gold)
```

On the sample in `tests/data` (a small bilingual newsletter I wrote, with a merge, a split, an added and an omitted sentence), the default mode gets all 34 beads right, `--mode flat` 32 and `--classic` 30. It's small and I wrote it myself, so it is a regression test, not a benchmark.

## Tests

58 unit and integration tests with a small built-in harness (`tests/minitest.hpp`): UTF-8/UTF-16, the DEFLATE decoder against streams made with Python's `zlib`, a hand-made `.docx` with tracked changes and text boxes, the sentence rules, the cost function against hand-computed values, and round-trips of every output format. It builds without warnings with `-Wall -Wextra -Wpedantic` and passes under AddressSanitizer and UBSan.

## Known limits

- Length is the only real signal, so reordered sections need a human check (they usually get flagged).
- A short sentence added next to a translated one can end up inside a 2-1 or 1-2 bead.
- No `.doc`, `.rtf`, `.odt` or `.pdf`; headers, footers, footnotes and comments in `.docx` are not read.
- Sentence rules exist only for English and French.
- The default English→French ratio (1.15) is only used for very short documents and hasn't been calibrated on a big corpus.
- I haven't tested a native Windows build; paths outside the system code page may not open.

## References

- Gale, W. A., & Church, K. W. (1993). A program for aligning sentences in bilingual corpora. *Computational Linguistics*, 19(1), 75–102.
- TMX 1.4b specification (LISA OSCAR, 2005); RFC 1951 (DEFLATE); ECMA-376 (Office Open XML).

## License

MIT, see [LICENSE](LICENSE).
