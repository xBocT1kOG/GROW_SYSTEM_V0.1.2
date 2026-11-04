# GROW SYSTEM V0.2 — presentation

Three synchronized 10-slide decks: Russian (`RU`), Ukrainian (`UK`) and English (`EN`).
The story, geometry, diagrams, image, numerical values and references are shared.
Speaker notes are embedded on every slide. Planned delivery is 640 seconds
(10 min 40 s), with 50–80 seconds allocated per slide; rehearse for personal pace.

## Files and editing

- `GROW_SYSTEM_V0.2_Presentation_{RU,UK,EN}.pptx`: finished editable presentations.
- `content.mjs`: slide content; localized strings always follow `[RU, UK, EN]`.
- `notes.mjs`: localized speaking text and shared timing.
- `build.mjs`: shared layout, native shapes/connectors, export and validation.
- `validate.py`: read-only package, notes, numeric/geometry parity and image checks.

Text and diagrams are native PowerPoint objects. The real Blynk screenshot is an
embedded image; its English interface is intentionally identical in all versions.
No real prototype photographs were available in the repository materials searched.
No stock or synthetic prototype photos have been substituted.

## Slide map

| # | Topic | Main visual |
|---|---|---|
| 1 | Title | Large project title and three functional pillars |
| 2 | Problem | Operator-support statements and two European examples |
| 3 | Evolution | V0.1 / V0.2 comparison; V0.1 is telemetry only |
| 4 | Device | Native functional-zone block diagram |
| 5 | Architecture | Five producers → queue → sole control owner → drivers; snapshots below |
| 6 | Lighting | DAWN / IDLE / DUSK phase sequence and test parameters |
| 7 | Remote interface | Actual `../assets/blynk-dashboard.png` |
| 8 | Diagnostics | Metrics and ESP32 → HTTP endpoints → PC collector → CSV → analysis |
| 9 | Result | Five functions connected to one system |
| 10 | Future | Professional / greenhouse and home / compact branches |

## Sources and claim boundaries

Primary technical sources: `../device-overview-{ru,uk,en}.md` and their corresponding
technical overview PDFs. Project evolution and future concepts also follow the
approved presentation brief. V0.2 is an experimental second prototype, not a final
commercial specification. LDR is measurement-only; pump control is manual.
Full-system 48–72 hour endurance validation is still pending. An earlier run with
incomplete peripherals is not presented as full-system validation. Future LLM
analysis sits above deterministic control and is not a current capability.

External context is confined to slide 2, with clickable footer citations and full
URLs in notes (checked 2026-11-04):

- [EU CAP Network: IRRISMA, Greece](https://eu-cap-network.ec.europa.eu/good-practice/smart-water-management-system-improve-crop-yield-while-using-less-water_en)
- [European Commission CORDIS: WATERBEE, project 283638](https://cordis.europa.eu/project/id/283638/reporting)

Their results are illustrative context, not expected GROW SYSTEM performance.

## Rebuild

Requires the Codex bundled Node.js runtime, `@oai/artifact-tool`, the presentations
skill finalizer, Python and Arial. No Python PowerPoint authoring library is used.
From the repository root, provide actual paths from the runtime inventory:

```powershell
& '<NODE_EXE>' docs/presentation/build.mjs '<NODE_MODULES>' '<PRESENTATIONS_SKILL_DIR>' '<PYTHON_EXE>' '.pio/presentation-build/new-output'
& '<PYTHON_EXE>' docs/presentation/validate.py
```

Use a new output directory for each revision: the finalizer deliberately refuses
to overwrite a finished output. After review, copy the three validated files into
this directory unchanged. Preview PNGs, layout evidence, drafts, logs and private
validation receipts remain in ignored `.pio/presentation-build/`.
The finalizer may need permission for Windows hard-link creation.

## Verification performed

- All 30 slides rendered at 1280 × 720 and visually reviewed; affected slides
  re-rendered and reviewed after fixing long Cyrillic labels.
- Package integrity, intended slide size, heading geometry, Arial policy and
  Artifact Tool re-import checks passed for each final deck.
- Shared layout and numeric parity, ten speaker-note pages and common embedded
  Blynk image checked from PPTX packages.
- Technical/status claims reviewed against the completed documents and brief.
- No firmware, partition configuration or PC collector functionality changed.

These checks do not claim a native Microsoft PowerPoint opening test. Optional
final polish: rehearse on the actual presentation machine and projector; add real
project photographs to slides 1/4/9 once available, preserving language parity.
