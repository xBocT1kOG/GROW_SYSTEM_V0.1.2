"""Read-only checks for delivered PPTX packages; standard-library Python only."""
from pathlib import Path
import hashlib
import re
import xml.etree.ElementTree as ET
import zipfile

HERE = Path(__file__).resolve().parent
NS = {'p': 'http://schemas.openxmlformats.org/presentationml/2006/main',
      'a': 'http://schemas.openxmlformats.org/drawingml/2006/main'}
SOURCES = ('eu-cap-network.ec.europa.eu', 'cordis.europa.eu/project/id/283638/reporting')
reference = None
media_reference = None
for lang in ('RU', 'UK', 'EN'):
    file = HERE / f'GROW_SYSTEM_V0.2_Presentation_{lang}.pptx'
    with zipfile.ZipFile(file) as z:
        assert z.testzip() is None, f'{lang}: invalid archive'
        slide_names = [n for n in z.namelist() if re.fullmatch(r'ppt/slides/slide\d+\.xml', n)]
        note_names = [n for n in z.namelist() if re.fullmatch(r'ppt/notesSlides/notesSlide\d+\.xml', n)]
        assert len(slide_names) == len(note_names) == 10
        rows = []
        for i in range(1, 11):
            root = ET.fromstring(z.read(f'ppt/slides/slide{i}.xml'))
            words = '\n'.join(x.text or '' for x in root.findall('.//a:t', NS))
            note_root = ET.fromstring(z.read(f'ppt/notesSlides/notesSlide{i}.xml'))
            note = '\n'.join(x.text or '' for x in note_root.findall('.//a:t', NS))
            assert len(note.split()) >= 85, f'{lang}/{i}: insufficient notes'
            assert not re.search(r'\b(?:TODO|TBD|Lorem ipsum)\b', words + note)
            assert '\ufffd' not in words + note
            if i == 2:
                assert all(source in note for source in SOURCES)
                rels = z.read('ppt/slides/_rels/slide2.xml.rels').decode()
                assert all(source in rels for source in SOURCES)
            if i == 5:
                for task in ('sensor_task','input_task','time_task','ramp_task','network_task','control_task'):
                    assert task in words
                assert len(root.findall('.//p:cxnSp', NS)) >= 10
            if i == 6:
                for value in ('06:00', '20:00', '60%', '0%', '10', '5', 'LIGHT_S', 'LIGHT_A', 'AUTO', 'MOD'):
                    assert value in words
            assert len(root.findall('.//p:pic', NS)) == (1 if i == 7 else 0)
            geometry = [tuple(sorted(el.attrib.items())) for el in root.findall('.//a:xfrm/*', NS)]
            numbers = sorted(re.findall(r'\d+(?:[.:]\d+)*', words))
            rows.append((geometry, numbers, len(root.findall('.//p:sp', NS)), len(root.findall('.//p:cxnSp', NS))))
        media = sorted(hashlib.sha256(z.read(n)).hexdigest() for n in z.namelist() if n.startswith('ppt/media/'))
        assert media, 'Blynk screenshot missing'
        if reference is None:
            reference, media_reference = rows, media
        else:
            assert rows == reference, f'{lang}: geometry / numeric parity mismatch'
            assert media == media_reference, f'{lang}: embedded image differs'
        all_xml = '\n'.join(z.read(n).decode('utf-8') for n in z.namelist() if n.endswith('.xml'))
        assert not re.search(r'(?:password|auth_token|blynk_token)\s*[:=]\s*["\']', all_xml, re.I)
    print(f'{lang}: 10 slides, 10 notes, native diagrams, sources, image and numeric/geometry parity PASS')
print('Presentation checks PASS. Visual review and factual review remain separate checks.')
