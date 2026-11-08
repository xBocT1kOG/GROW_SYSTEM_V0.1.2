"""Independently recompute key results from raw CSV and inspect final PDF geometry."""
import csv
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import statistics
import re
from pypdf import PdfReader
import pdfplumber

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
s=json.loads((HERE/'summary.json').read_text(encoding='utf-8'))
source=ROOT/s['source']
assert hashlib.sha256(source.read_bytes()).hexdigest()==s['sha256']
with source.open(encoding='utf-8-sig',newline='') as stream:
    rows=list(csv.DictReader(stream))
assert len(rows)==s['rows']
epochs=[Decimal(r['timestamp_epoch']) for r in rows]
assert float(epochs[-1]-epochs[0])==s['duration_seconds']
intervals=[b-a for a,b in zip(epochs,epochs[1:])]
assert float(statistics.median(intervals))==s['intervals']['median']
assert float(max(intervals))==s['intervals']['max']
assert sum(x>15 for x in intervals)==len(s['intervals']['gaps'])
for key,values in s['counters'].items():
    column=[int(r['diag_'+key]) for r in rows]
    assert [column[0],column[-1],min(column),max(column)]==[values[k] for k in ('start','final','min','max')]
    differences=[b-a for a,b in zip(column,column[1:])]
    assert sum(differences)==values['delta']
    assert sum(d<0 for d in differences)==values['decreases']==0
    if 'events' in values:
        assert [i+2 for i,d in enumerate(differences) if d]==[e['row'] for e in values['events']]
for task,values in s['stacks'].items():
    assert min(int(r[f'diag_stack_{task}_bytes']) for r in rows)==values['min']>0
for field in ('heap_free_bytes','heap_min_free_bytes'):
    column=[int(r['diag_'+field]) for r in rows]
    assert [column[0],column[-1],min(column),max(column)]==[s['heap'][field][k] for k in ('start','final','min','max')]
uptime=[int(r['diag_uptime_seconds']) for r in rows]
assert [i+2 for i,(a,b) in enumerate(zip(uptime,uptime[1:])) if b<a]==s['uptime']['resets']==[]
assert uptime[-1]-uptime[0]==s['uptime']['delta']
assert all(int(r['collector_total_samples'])==i+1 for i,r in enumerate(rows))
for k,expected in [(2,s['collector']['full']),(1,s['collector']['partial']),(0,s['collector']['failed'])]:
    assert sum(int(r['state_request_ok'])+int(r['diagnostics_request_ok'])==k for r in rows)==expected
assert sum(int(r['request_ok']) for r in rows)==s['collector']['full']
assert len(set(r['timestamp_local'] for r in rows))==len(rows)
assert not any('C:\\Users\\' in f.read_text(encoding='utf-8') for f in [HERE/'endurance-report-ru.md',HERE/'summary.json'])
pdf=HERE/'GROW_SYSTEM_V0.2_Endurance_Test_Report_RU.pdf'
reader=PdfReader(pdf)
assert len(reader.pages)==9
text='\n'.join(p.extract_text() for p in reader.pages)
for token in ('калибровк','HC-SR04','179 016','172 220','19 ч 56 мин 32,297 с','7 180','Wi-Fi'):
    assert token in text, f'Missing report content: {token}'
assert not re.search(r'\b(?:TODO|TBD|Lorem ipsum)\b',text)
assert '\ufffd' not in text and 'C:\\Users\\' not in text
with pdfplumber.open(pdf) as parsed:
    for i,page in enumerate(parsed.pages,1):
        assert all(40<=ch['x0']<=ch['x1']<=page.width-40 and 20<=ch['top']<=ch['bottom']<=page.height-15 for ch in page.chars), f'Out of bounds on page {i}'
        assert str(i) in page.extract_text().splitlines()[-1], f'Page number missing: {i}'
print('PASS: full CSV independently recomputed; counter deltas / event rows / uptime / heap / stacks / collector agree.')
print('PASS: original SHA-256, 9 A4 pages, Cyrillic extraction and text bounds. Visual review required separately.')
