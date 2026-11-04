"""Build synchronized A4 PDFs directly from the editable Markdown sources."""
import argparse
from html import escape
from pathlib import Path
import re

from PIL import Image as PILImage
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, Frame, PageTemplate, Paragraph,
                               Spacer, PageBreak, Table, TableStyle, Image, KeepTogether)
from reportlab.platypus.tableofcontents import TableOfContents

DOCS = Path(__file__).resolve().parents[1]
INK = colors.HexColor('#16334a')
ACCENT = colors.HexColor('#207d82')
MUTED = colors.HexColor('#566876')
WIDTH = A4[0] - 104


class Overview(BaseDocTemplate):
    def afterFlowable(self, flowable):
        if isinstance(flowable, Paragraph) and flowable.style.name == 'Section':
            title = flowable.getPlainText()
            key = 'section-' + title.split('.')[0]
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(title, key, 0)
            self.notify('TOCEntry', (0, title, self.page, key))


def footer(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor('#d8e3e8'))
    canvas.line(52, 40, A4[0] - 52, 40)
    canvas.setFont('Body', 8)
    canvas.setFillColor(MUTED)
    canvas.drawString(52, 27, 'GROW SYSTEM V0.2')
    canvas.drawRightString(A4[0] - 52, 27, str(doc.page))
    if doc.page == 1:
        canvas.setFillColor(ACCENT)
        canvas.rect(52, A4[1] - 100, 66, 5, fill=1, stroke=0)
    canvas.restoreState()


def inline(text):
    text = escape(text)
    return re.sub(r'`([^`]+)`', r'<font name="Code">\1</font>', text)


def build(lang, output):
    raw = (DOCS / f'device-overview-{lang}.md').read_text(encoding='utf-8-sig')
    chunks = re.split(r'^## ', raw, flags=re.M)
    subtitle = chunks[0].strip().split('\n\n')[1]
    sections = [chunk.strip().split('\n', 1) for chunk in chunks[1:]]
    styles = {
        'body': ParagraphStyle('BodyText', fontName='Body', fontSize=10.5, leading=14.8,
                               textColor=INK, spaceAfter=10, splitLongWords=False),
        'heading': ParagraphStyle('Section', fontName='Body-Bold', fontSize=17, leading=21,
                                  textColor=INK, spaceBefore=12, spaceAfter=16, keepWithNext=True),
        'caption': ParagraphStyle('Caption', fontName='Body', fontSize=8.5, leading=11,
                                  textColor=MUTED, spaceAfter=12),
        'tablecaption': ParagraphStyle('TableCaption', fontName='Body-Bold', fontSize=9,
                                       leading=12, textColor=INK, spaceBefore=6, spaceAfter=7,
                                       keepWithNext=True),
        'cell': ParagraphStyle('Cell', fontName='Body', fontSize=8.9, leading=11.5, textColor=INK),
        'headcell': ParagraphStyle('HeadCell', fontName='Body-Bold', fontSize=8.9, leading=11.5,
                                   textColor=colors.white),
    }
    story = [Spacer(1, 140),
             Paragraph('GROW SYSTEM<br/>V0.2', ParagraphStyle('Title', fontName='Body-Bold',
                       fontSize=39, leading=46, textColor=INK, spaceAfter=28)),
             Paragraph(subtitle, ParagraphStyle('Subtitle', fontName='Body', fontSize=21,
                       leading=29, textColor=ACCENT, spaceAfter=24)),
             Paragraph('ESP32-S3 / FreeRTOS', ParagraphStyle('Platform', fontName='Body',
                       fontSize=12, leading=18, textColor=MUTED)), PageBreak()]
    toc = TableOfContents()
    toc.levelStyles = [ParagraphStyle('ContentsEntry', fontName='Body', fontSize=11,
                                     leading=17, spaceBefore=10, textColor=INK)]
    story += [Paragraph({'ru': 'Содержание', 'uk': 'Зміст', 'en': 'Contents'}[lang],
                        ParagraphStyle('ContentsTitle', parent=styles['heading'])), toc, PageBreak()]

    # Common section starts keep the three languages comparable.
    starts = {4, 5, 7, 8, 9, 10, 11}
    for number, (heading, content) in enumerate(sections, 1):
        if number in starts:
            story.append(PageBreak())
        story.append(Paragraph(inline(heading), styles['heading']))
        blocks = re.split(r'\n\s*\n', content.strip())
        index = 0
        while index < len(blocks):
            block = blocks[index].strip()
            if block.startswith('|'):
                rows = [[cell.strip() for cell in line.strip('|').split('|')]
                        for line in block.splitlines()]
                rows.pop(1)
                widths = [WIDTH * .22, WIDTH * .78] if len(rows[0]) == 2 else (
                    [WIDTH * .10, WIDTH * .50, WIDTH * .40] if number == 9 else
                    [WIDTH * .25, WIDTH * .35, WIDTH * .40])
                cells = [[Paragraph(inline(cell), styles['headcell' if i == 0 else 'cell'])
                          for cell in row] for i, row in enumerate(rows)]
                table = Table(cells, colWidths=widths, repeatRows=1, hAlign='LEFT', splitByRow=0)
                table.setStyle(TableStyle([
                    ('BACKGROUND', (0, 0), (-1, 0), INK),
                    ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.HexColor('#edf3f6'), colors.white]),
                    ('VALIGN', (0, 0), (-1, -1), 'TOP'),
                    ('LEFTPADDING', (0, 0), (-1, -1), 8),
                    ('RIGHTPADDING', (0, 0), (-1, -1), 8),
                    ('TOPPADDING', (0, 0), (-1, -1), 7),
                    ('BOTTOMPADDING', (0, 0), (-1, -1), 7),
                ]))
                story += [table, Spacer(1, 12)]
            elif block.startswith('!['):
                path = DOCS / re.search(r'\]\(([^)]+)\)', block).group(1)
                with PILImage.open(path) as picture:
                    width, height = picture.size
                limit = {5: 270, 7: 350, 8: 330, 9: 245, 10: 180}[number]
                scale = min(WIDTH / width, limit / height)
                figure = Image(str(path), width=width * scale, height=height * scale)
                caption = Paragraph(inline(blocks[index + 1]), styles['caption'])
                story.append(KeepTogether([figure, Spacer(1, 7), caption]))
                index += 1
            elif block.startswith('- '):
                for line in block.splitlines():
                    story.append(Paragraph('• ' + inline(line[2:]), styles['body']))
            elif re.match(r'(Таблица|Таблиця|Table) \d', block):
                story.append(Paragraph(inline(block), styles['tablecaption']))
            else:
                story.append(Paragraph(inline(block), styles['body']))
            index += 1

    target = output / f'GROW_SYSTEM_V0.2_Technical_Overview_{lang.upper()}.pdf'
    doc = Overview(str(target), pagesize=A4, leftMargin=52, rightMargin=52,
                   topMargin=48, bottomMargin=54, title='GROW SYSTEM V0.2 - ' + subtitle,
                   author='', subject=subtitle, lang={'ru': 'ru-RU', 'uk': 'uk-UA', 'en': 'en'}[lang])
    frame = Frame(52, 54, WIDTH, A4[1] - 102, leftPadding=0, rightPadding=0,
                  topPadding=0, bottomPadding=0)
    doc.addPageTemplates(PageTemplate(id='A4', frames=frame, onPage=footer))
    doc.multiBuild(story)
    print(target.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fonts', type=Path, default=Path('C:/Windows/Fonts'))
    parser.add_argument('--output', type=Path, default=DOCS)
    args = parser.parse_args()
    for name, file in [('Body', 'arial.ttf'), ('Body-Bold', 'arialbd.ttf'), ('Code', 'consola.ttf')]:
        pdfmetrics.registerFont(TTFont(name, str(args.fonts / file)))
    args.output.mkdir(parents=True, exist_ok=True)
    for lang in ('ru', 'uk', 'en'):
        build(lang, args.output)


if __name__ == '__main__':
    main()
