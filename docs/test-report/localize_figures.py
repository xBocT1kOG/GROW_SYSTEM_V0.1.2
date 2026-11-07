"""Translate existing SVG text only; never read the CSV or recalculate plots."""
from html import escape, unescape
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
FIGURES = ('health-summary', 'error-counters', 'heap', 'stack-margins', 'lighting-activity')
# Each pair is Ukrainian, English. Geometry, values and original assets are untouched.
LABELS = {
    'Наблюдение': ('Спостереження', 'Observation'),
    '19 ч 56 мин': ('19 год 56 хв', '19 h 56 min'),
    'Коллектор': ('Колектор', 'Collector'),
    '7180 полных опросов': ('7180 повних опитувань', '7180 full samples'),
    'Очередь / сроки': ('Черга / терміни', 'Queue / deadlines'),
    'Прирост: 0 / 0': ('Приріст: 0 / 0', 'Delta: 0 / 0'),
    'Без явного истощения': ('Без явного виснаження', 'No obvious depletion'),
    'Стеки': ('Стеки', 'Stacks'),
    'Все остатки >0': ('Усі залишки >0', 'All margins >0'),
    'Новых подключений: 0': ('Нових підключень: 0', 'New connections: 0'),
    'Тайм-аутов: 3': ('Тайм-аутів: 3', 'Timeouts: 3'),
    'Граница вывода': ('Межа висновку', 'Conclusion scope'),
    'Только данный прогон': ('Лише цей прогін', 'This run only'),
    'Счётчик': ('Лічильник', 'Counter'),
    'Накопительные счётчики: начальное значение и изменения': (
        'Накопичувальні лічильники: початкове значення та зміни',
        'Cumulative counters: initial value and changes'),
    'Wi-Fi: значение с загрузки': ('Wi-Fi: від завантаження', 'Wi-Fi: value since boot'),
    'Blynk poll/upload, очередь, сроки: все 0': (
        'Blynk poll/upload, черга, терміни: усі 0', 'Blynk poll/upload, queue, deadlines: all 0'),
    '06.11.2026, время коллектора (UTC+02:00)': (
        '06.11.2026, час колектора (UTC+02:00)', '06.11.2026, collector time (UTC+02:00)'),
    'HC-SR04: ошибки = тайм-ауты': ('HC-SR04: помилки = тайм-аути', 'HC-SR04: errors = timeouts'),
    'Свободная память, КиБ (1024 байта)': ('Вільна пам’ять, КіБ (1024 байти)', 'Free memory, KiB (1024 bytes)'),
    'Память: все 7 180 диагностических снимков': (
        'Пам’ять: усі 7 180 діагностичних знімків', 'Memory: all 7 180 diagnostic snapshots'),
    'Текущая свободная heap': ('Поточна вільна heap', 'Current free heap'),
    'Минимум с загрузки': ('Мінімум від завантаження', 'Minimum since boot'),
    'LIGHT_S, % команды': ('LIGHT_S, % команди', 'LIGHT_S, % commanded'),
    'Командный уровень света и состояние автоматики': (
        'Заданий рівень світла та стан автоматики', 'Commanded light level and automation state'),
    'Минимальный наблюдённый свободный остаток, байты': (
        'Мінімальний спостережуваний вільний залишок, байти', 'Minimum observed free margin, bytes'),
    'Запас стека семи задач': ('Запас стека семи задач', 'Stack margins of seven tasks'),
}

def localize():
    used = set()
    for stem in FIGURES:
        source = (HERE/'assets'/f'{stem}.svg').read_text(encoding='utf-8')
        for index, lang in enumerate(('uk', 'en')):
            def replace(match):
                value = unescape(match[2])
                if value in LABELS:
                    used.add(value)
                    value = LABELS[value][index]
                elif re.search('[А-Яа-яЁё]', value):
                    raise ValueError(f'Untranslated label: {value}')
                return match[1] + escape(value, quote=False) + match[3]
            result = re.sub(r'(<text\b[^>]*>)(.*?)(</text>)', replace, source, flags=re.S)
            (HERE/'assets'/f'{stem}-{lang}.svg').write_text(result, encoding='utf-8')
    assert used == set(LABELS), f'Unused translations: {set(LABELS)-used}'
    print('Localized 10 SVGs; original geometry and values retained.')

if __name__ == '__main__':
    localize()
