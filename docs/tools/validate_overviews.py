"""Check structural parity, numerical parity, assets and exported PDF text."""
from collections import Counter
from pathlib import Path
import re

import pdfplumber
from pypdf import PdfReader

DOCS = Path(__file__).resolve().parents[1]


def signature(text):
    sections = re.split(r'^## ', text, flags=re.M)[1:]
    result = []
    for section in sections:
        # Decimal separators vary by language; image language suffixes do not carry facts.
        normalized = re.sub(r'(?<=\d),(?=\d)', '.', section)
        numbers = Counter(re.findall(r'\d+(?:\.\d+)?', normalized))
        identifiers = Counter(re.findall(r'`([^`]+)`', section))
        tables = [line for line in section.splitlines() if line.startswith('|')]
        cells = [len(line.split('|')) for line in tables]
        figures = re.findall(r'!\[[^]]*\]\(([^)]+)\)', section)
        result.append((numbers, identifiers, cells,
                       [re.sub(r'-(ru|uk|en)\.', '-LANG.', f) for f in figures]))
    return result


def main():
    reference = None
    page_counts = []
    for lang in ('ru', 'uk', 'en'):
        text = (DOCS / f'device-overview-{lang}.md').read_text(encoding='utf-8-sig')
        assert re.findall(r'^## (\d+)\.', text, re.M) == [str(n) for n in range(1, 13)]
        assert re.findall(r'^(?:Рисунок|Figure) (\d+) ', text, re.M) == list('12345')
        assert re.findall(r'^(?:Таблица|Таблиця|Table) (\d+) ', text, re.M) == list('1234')
        assert re.findall(r'^\| (V\d) \|', text, re.M) == ['V' + str(n) for n in range(9)]
        for image in re.findall(r'!\[[^]]*\]\(([^)]+)\)', text):
            assert (DOCS / image).is_file(), image
        for stale in ('4019', '4\u202f019', '20\u202f091', '179\u202f148', '2026-11-04_04-14',
                      'test-light-telemetry', 'test-climate-telemetry', 'preliminary-test-summary'):
            assert stale not in text, stale
        current = signature(text)
        if reference is None:
            reference = current
        else:
            assert current == reference, f'{lang}: section facts/structure differ'
        pdf = DOCS / f'GROW_SYSTEM_V0.2_Technical_Overview_{lang.upper()}.pdf'
        reader = PdfReader(pdf)
        page_counts.append(len(reader.pages))
        all_text = '\n'.join(page.extract_text() for page in reader.pages)
        assert '\ufffd' not in all_text and '\u25a0' not in all_text
        for token in ('LIGHT_S', 'LIGHT_A', '/api/state', '/api/diagnostics', 'PC_collector'):
            assert token in all_text, token
        if lang == 'uk':
            assert all(char in all_text for char in 'іїєґ'), 'Missing Ukrainian character'
        with pdfplumber.open(pdf) as document:
            for page in document.pages:
                assert len(page.extract_text() or '') > 50, 'Empty page'
                assert abs(page.width - 595.28) < 1 and abs(page.height - 841.89) < 1
                for char in page.chars:
                    assert 45 <= char['x0'] <= char['x1'] <= page.width - 45, 'Horizontal overflow'
                    assert 15 <= char['top'] < char['bottom'] <= page.height - 15, 'Vertical overflow'
        print(f'{lang.upper()}: 12 sections, 5 figures, 4 tables; {len(reader.pages)} A4 pages: PASS')
    assert len(set(page_counts)) == 1, 'Pagination differs'
    # Same diagram topology across languages, ignoring translated labels.
    for kind in ('hardware-architecture', 'software-architecture', 'light-control', 'diagnostics-flow'):
        sources = [(DOCS / 'assets' / f'{kind}-{lang}.mmd').read_text(encoding='utf-8')
                   for lang in ('ru', 'uk', 'en')]
        topology = [re.sub(r'"[^"]*"', '"LABEL"', s) for s in sources]
        assert len(set(topology)) == 1, kind
    print('Cross-language section numbers, numeric facts, identifiers, tables and diagram topology: PASS')


if __name__ == '__main__':
    main()
