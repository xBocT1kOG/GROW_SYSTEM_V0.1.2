"""Check editorial/figure parity without reading or reanalysing the raw log.

Meaning, status classifications and typography also require human review.
"""
from collections import Counter
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from PIL import Image
from localize_figures import FIGURES, LABELS

HERE = Path(__file__).resolve().parent

def numbers(text):
    # Decimal punctuation may differ; thousands groups retain their digits.
    text = re.sub(r'(?<=\d)[ ,](?=\d{3}(?:\D|$))',
                  lambda m: '' if m[0] == ' ' else '.', text)
    return re.findall(r'\d+(?:[.,]\d+)*', text.replace(',', '.'))

def blocks(text):
    return re.split(r'\n\s*\n', text.strip())

def geometry(root):
    for node in root.iter():
        if node.tag.endswith('}text'):
            node.text = ''
    return ET.tostring(root)

def check_markdown():
    source = (HERE/'endurance-report-ru.md').read_text(encoding='utf-8-sig')
    base = blocks(source)
    for lang, table, figure in [('uk','Таблиця','Рисунок'),('en','Table','Figure')]:
        text = (HERE/f'endurance-report-{lang}.md').read_text(encoding='utf-8-sig')
        translated = blocks(text)
        assert len(base) == len(translated), (lang,'paragraph count')
        for i, (left, right) in enumerate(zip(base, translated), 1):
            assert Counter(numbers(left)) == Counter(numbers(right)), (lang, i, numbers(left), numbers(right))
            assert re.findall(r'`([^`]+)`',left) == re.findall(r'`([^`]+)`',right), (lang,i,'identifiers/paths')
            if left.startswith('|'):
                lr = [line.split('|')[1:-1] for line in left.splitlines()]
                rr = [line.split('|')[1:-1] for line in right.splitlines()]
                assert len(lr) == len(rr), (lang,i,'table rows')
                for j,(a,b) in enumerate(zip(lr,rr)):
                    assert len(a) == len(b), (lang,i,j,'columns')
                    for x,y in zip(a,b):
                        assert numbers(x) == numbers(y), (lang,i,j,'cell values')
        assert re.findall(r'^## (\d+)\.',text,re.M) == [str(i) for i in range(1,9)]
        assert re.findall(rf'^{table} (\d+)\.',text,re.M) == [str(i) for i in range(1,9)]
        assert re.findall(rf'^{figure} (\d+)\.',text,re.M) == [str(i) for i in range(1,6)]
        assert sum(b.startswith('|') for b in translated) == 9
        assert text.count('<!-- pagebreak -->') == 8
        assert text.count('![') == 5
        if lang == 'en':
            assert not re.search('[А-Яа-яЁёІіЇїЄєҐґ]',text)
        else:
            assert not re.search('[ыэъёЫЭЪЁ]',text)
            assert all(c in text.lower() for c in 'іїєґ')
        print(f'{lang}: paragraph numeric parity; table cell/order parity; code/path parity; 8 sections, 8 numbered tables + contents, 5 figures.')

def check_figures():
    ns = {'s':'http://www.w3.org/2000/svg'}
    for stem in FIGURES:
        original = HERE/'assets'/f'{stem}.svg'
        labels = [n.text for n in ET.parse(original).findall('.//s:text',ns)]
        for index,lang in enumerate(('uk','en')):
            localized = HERE/'assets'/f'{stem}-{lang}.svg'
            actual = [n.text for n in ET.parse(localized).findall('.//s:text',ns)]
            expected = [LABELS[t][index] if t in LABELS else t for t in labels]
            assert actual == expected, (stem,lang,'labels')
            assert geometry(ET.parse(original).getroot()) == geometry(ET.parse(localized).getroot()), (stem,lang,'geometry')
            with Image.open(original.with_suffix('.png')) as a, Image.open(localized.with_suffix('.png')) as b:
                assert a.size == b.size, (stem,lang,'raster size')
    print('10 localized figures: all non-text SVG content, coordinates and plotted values identical; PNG dimensions match RU.')

if __name__ == '__main__':
    check_markdown()
    check_figures()
