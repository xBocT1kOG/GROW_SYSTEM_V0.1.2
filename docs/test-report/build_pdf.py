"""Render the editable Markdown report to A4 PDF. Requires reportlab and Pillow."""
import argparse
from html import escape
from pathlib import Path
import re
from PIL import Image as PILImage
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, PageBreak,
                               Table, TableStyle, Image, KeepTogether)

HERE=Path(__file__).resolve().parent
INK=colors.HexColor('#183344'); TEAL=colors.HexColor('#207D82'); MUTED=colors.HexColor('#526775')
WIDTH=A4[0]-88

def inline(value):
    value=escape(value)
    value=re.sub(r'`([^`]+)`',r'<font name="Code">\1</font>',value)
    return re.sub(r'\*\*([^*]+)\*\*',r'<b>\1</b>',value)

def footer(canvas,doc):
    canvas.saveState();canvas.setStrokeColor(colors.HexColor('#D3E0E5'))
    canvas.line(44,38,A4[0]-44,38);canvas.setFont('Body',8);canvas.setFillColor(MUTED)
    canvas.drawString(44,25,doc.report_footer)
    canvas.drawRightString(A4[0]-44,25,str(doc.page));canvas.restoreState()

def build(font_dir, lang='ru'):
    for name,file in [('Body','arial.ttf'),('Body-Bold','arialbd.ttf'),('Code','consola.ttf')]:
        pdfmetrics.registerFont(TTFont(name,str(font_dir/file)))
    pdfmetrics.registerFontFamily('Body',normal='Body',bold='Body-Bold',italic='Body',boldItalic='Body-Bold')
    styles={
      'body':ParagraphStyle('body',fontName='Body',fontSize=10.1,leading=13.7,textColor=INK,spaceAfter=8),
      'heading':ParagraphStyle('heading',fontName='Body-Bold',fontSize=17,leading=21,textColor=INK,spaceAfter=13,keepWithNext=True),
      'sub':ParagraphStyle('sub',fontName='Body-Bold',fontSize=11.5,leading=15,textColor=TEAL,spaceBefore=7,spaceAfter=8,keepWithNext=True),
      'caption':ParagraphStyle('caption',fontName='Body',fontSize=8.7,leading=11.3,textColor=MUTED,spaceAfter=10),
      'tablecaption':ParagraphStyle('tablecaption',fontName='Body-Bold',fontSize=9,leading=12,textColor=INK,spaceBefore=2,spaceAfter=6,keepWithNext=True),
      'cell':ParagraphStyle('cell',fontName='Body',fontSize=9,leading=11.4,textColor=INK),
      'headcell':ParagraphStyle('headcell',fontName='Body-Bold',fontSize=9,leading=11.4,textColor=colors.white),
    }
    pages=(HERE/f'endurance-report-{lang}.md').read_text(encoding='utf-8').split('<!-- pagebreak -->')
    story=[]
    for pi,page in enumerate(pages):
        if pi:story.append(PageBreak())
        else:story.append(Spacer(1,85))
        blocks=re.split(r'\n\s*\n',page.strip())
        for block in blocks:
            block=block.strip()
            if block.startswith('# '):
                story.append(Paragraph(inline(block[2:]),ParagraphStyle('title',fontName='Body-Bold',fontSize=32,leading=39,textColor=INK,spaceAfter=25)))
            elif block.startswith('## '):
                style=styles['heading'] if pi else ParagraphStyle('subtitle',fontName='Body',fontSize=23,leading=30,textColor=TEAL,spaceAfter=26)
                story.append(Paragraph(inline(block[3:]),style))
            elif block.startswith('### '):story.append(Paragraph(inline(block[4:]),styles['sub']))
            elif block.startswith('|'):
                rows=[[v.strip() for v in line.strip('|').split('|')] for line in block.splitlines()]
                rows.pop(1);heads=rows[0]
                if len(heads)==2:ratios=[.48,.52]
                elif len(heads)==3:ratios=[.34,.29,.37] if heads[0] in ('Подсистема','Підсистема','Subsystem') else [.46,.27,.27]
                else:
                    ratios=[.48,.17,.17,.18] if heads[1] in ('Начало','Початок','Initial') else [.20,.20,.25,.35]
                data=[[Paragraph(inline(v),styles['headcell' if j==0 else 'cell']) for v in row] for j,row in enumerate(rows)]
                tab=Table(data,colWidths=[WIDTH*r for r in ratios],repeatRows=1,hAlign='LEFT')
                tab.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),TEAL),('VALIGN',(0,0),(-1,-1),'TOP'),
                    ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.HexColor('#F1F5F7'),colors.white]),
                    ('LINEBELOW',(0,-1),(-1,-1),.5,colors.HexColor('#D3E0E5')),
                    ('LEFTPADDING',(0,0),(-1,-1),7),('RIGHTPADDING',(0,0),(-1,-1),7),
                    ('TOPPADDING',(0,0),(-1,-1),5),('BOTTOMPADDING',(0,0),(-1,-1),5)]))
                story.extend([tab,Spacer(1,10)])
            elif block.startswith('!['):
                match=re.fullmatch(r'!\[(.*?)\]\((.*?)\)',block)
                file=HERE/match[2]
                with PILImage.open(file) as image:w,h=image.size
                story.extend([Image(str(file),width=WIDTH,height=WIDTH*h/w),Spacer(1,4)])
            elif block.startswith('- '):
                for line in block.splitlines():
                    story.append(Paragraph('• '+inline(line[2:]),styles['body']))
            else:
                kind='tablecaption' if block.startswith(('Таблица ','Таблиця ','Table ')) else 'caption' if block.startswith(('Рисунок ','Figure ')) else 'body'
                story.append(Paragraph(inline(block.replace('\n',' ')),styles[kind]))
    target=HERE/f'GROW_SYSTEM_V0.2_Endurance_Test_Report_{lang.upper()}.pdf'
    titles={'ru':'Отчёт о непрерывном тестировании и состоянии системы',
            'uk':'Звіт про безперервне тестування та стан системи',
            'en':'Continuous Endurance Test and System Health Report'}
    footers={'ru':'Непрерывное тестирование','uk':'Безперервне тестування','en':'Continuous endurance test'}
    doc=SimpleDocTemplate(str(target),pagesize=A4,rightMargin=44,leftMargin=44,topMargin=45,bottomMargin=49,
        title='GROW SYSTEM V0.2 - '+titles[lang],author='',pageCompression=1)
    doc.report_footer='GROW SYSTEM V0.2 | '+footers[lang]
    doc.build(story,onFirstPage=footer,onLaterPages=footer)
    print(target.name)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font-dir',type=Path,default=Path('C:/Windows/Fonts'))
    parser.add_argument('--lang',choices=('ru','uk','en'),default='ru')
    args=parser.parse_args()
    build(args.font_dir,args.lang)
