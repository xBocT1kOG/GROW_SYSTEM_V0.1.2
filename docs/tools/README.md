# Документация RU / UK / EN

Редактируемые источники: `docs/device-overview-{ru,uk,en}.md`. Во всех версиях
12 разделов, 5 рисунков и 4 таблицы в одинаковом порядке. Результаты старого
CSV-прогона исключены: аппаратная конфигурация не была полной. Исходные журналы
и PC_collector не изменяются. Отдельный отчёт испытаний здесь не создаётся.

## Схемы

Источники Mermaid находятся в `docs/assets/*-{ru,uk,en}.mmd`. Для одинаковой
схемы сохраняйте узлы и связи; переводите только подписи. Результат - SVG и PNG.
Использованы Mermaid 11.12.2, Playwright 1.62.1 и Microsoft Edge. Из корня проекта:

```powershell
npm install --prefix .pio/docs-render playwright@1.62.1
Invoke-WebRequest https://cdn.jsdelivr.net/npm/mermaid@11.12.2/dist/mermaid.min.js -OutFile .pio/docs-render/mermaid.min.js
$env:NODE_PATH = (Resolve-Path .pio/docs-render/node_modules).Path
node docs/tools/render_diagrams.cjs .pio/docs-render/mermaid.min.js 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
```

При использовании уже установленного Playwright укажите его `node_modules`.
Рендерер проверяет Mermaid, запускает браузер без окна и использует локальный
скрипт без сетевых запросов. Идентификаторы и стиль фиксированы. Обновления браузера
и шрифтов могут изменить размеры: проверяйте результат после обновления.

`blynk-dashboard.png` - предоставленный пользователем оригинал, общий для трёх
языков. Не перерисовывайте панель и не принимайте её числа за результаты испытания.

## PDF

Требуются Python 3.11+, ReportLab и Pillow. Проверки используют pypdf и pdfplumber.
Шрифты Arial, Arial Bold и Consolas берутся из установленной системы, в Git не
добавляются. Две семьи поддерживают русский, украинский и английский текст.

```powershell
python -B -X utf8 docs/tools/build_overview_pdf.py --fonts C:/Windows/Fonts
python -B -X utf8 docs/tools/validate_overviews.py
```

PDF создаются непосредственно из Markdown, поэтому отдельного редактируемого
экземпляра текста в генераторе нет. A4, титульный лист, автоматическое содержание
с номерами страниц и закладками, подписи и нижний колонтитул формируются единообразно.
Параметр `--output` позволяет выбрать другой каталог вместо `docs/`.

После экспорта обязательно отрисуйте все страницы каждого языка через Poppler:

```powershell
pdftoppm -r 120 -png docs/GROW_SYSTEM_V0.2_Technical_Overview_RU.pdf .pio/docs-render/RU
```

Повторите для UK и EN. Проверьте переносы, таблицы, подписи, схемы и украинские
символы. Автоматическая проверка структуры и извлечённого текста не заменяет
визуальный просмотр. При изменении содержания редактируйте все три языка вместе.

## Будущий анализ данных

`generate_test_charts.py` и его тесты сохранены как вспомогательные инструменты
для отдельного будущего отчёта. Они не участвуют в построении обзоров и PDF.
Для анализа используйте только журнал с документированными условиями испытания:

```powershell
python -B -X utf8 docs/tools/generate_test_charts.py --source PC_collector/LOGS/RUN.csv --max-rows 1000 --output .pio/test-analysis
python -B -m unittest discover -s docs/tools -p "test_*.py"
```

`RUN.csv` и 1000 - примеры аргументов, не результаты измерений. Для графиков нужен
Matplotlib. Скрипт фиксирует префикс файла и его SHA-256, проверяет поля, выбирает
непрерывный участок и оставляет ошибки датчиков пропусками. Это техническая проверка
данных, а не подтверждение полноты аппаратной конфигурации. Raw CSV не коммитятся.
