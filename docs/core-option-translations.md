# Core Option Translations (Crowdin)

How the core's option labels and descriptions get translated, what is
wired up in this repository, and what is still owned by the libretro
organisation.

Upstream reference: [Adding
Translations](https://github.com/libretro/docs/blob/master/docs/development/cores/core-options-translation.md).
The `intl/` toolchain and the workflows are copies of the [RetroArch
sample](https://github.com/libretro/RetroArch/tree/master/libretro-common/samples/core_options/example_translation),
with the deviations noted below.

## How the pipeline works

English is the source of truth and lives in `libretro_core_options.h`.
Nothing in the translation pipeline ever edits that file.

1. **Upload** — `crowdin_source_upload.yml` runs when
   `libretro_core_options.h` changes on `develop`. It extracts the
   English strings and pushes them to the shared RetroArch Crowdin
   project.
2. **Translate** — humans translate on Crowdin.
3. **Sync** — `crowdin_translation_sync.yml` runs every Friday, pulls
   the translations, regenerates `libretro_core_options_intl.h`, and
   commits it to `develop`.
4. **Use** — the core already asks for
   `RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL` and passes
   `options_intl[language]`, so a translated string is picked up with no
   further code change *once its language is enabled* (see below).

## Enabling a language

This is the one manual step, and it is manual upstream too. The sync
only writes `libretro_core_options_intl.h`; it never touches the
`options_intl[]` table in `libretro_core_options.h`.

So after the first sync produces, say, `options_ja`, that language stays
inert until its `NULL` in `options_intl[]` is replaced:

```c
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
   &options_us, /* RETRO_LANGUAGE_ENGLISH */
   &options_ja, /* RETRO_LANGUAGE_JAPANESE */   <- was NULL
   ...
```

Order must match the `retro_language` enum in `libretro.h`. Every entry
is `NULL` today because no translations exist yet.

## What an org admin still has to do

The in-repo half is done; the pipeline cannot run until someone with
libretro org access completes these:

1. Ask a Crowdin project manager — on
   [Crowdin](https://crowdin.com/project/retroarch) or in the
   `retroarch-translations` Discord channel — for an access token scoped
   to this repository.
2. Add it as the Actions repository secret **`CROWDIN_API_KEY`**.
3. Run **Crowdin Translations Initial Setup** manually, once. It seeds
   Crowdin with the English strings. Running it more than once can
   overwrite newer Crowdin-side translations, so delete or disable that
   workflow afterwards.
4. Run **Crowdin Translation Sync** manually once to confirm it can push
   to `develop`. A `Permission to <repository> denied` error means
   `GITHUB_TOKEN` needs write access.

Until step 2 exists, both workflows run with an empty key and fail —
which is the intended, visible "not set up yet" state.

## Deviations from the upstream sample

Three, each deliberate:

- **The workflows watch and write `develop`, not the default branch.**
  This repo is GitFlow: `master` is release-only. The sample assumes a
  single default branch, and on a schedule it would check out and commit
  straight to `master`.
- **`on:` blocks were rewritten.** The sample ships
  `on:` / `workflow_dispatch` / `schedule:` as sibling lines, which is
  not valid YAML — the manual-dispatch trigger is silently lost.
- **`intl/activate.py` was removed.** It is a one-shot placeholder
  filler that targets the older `crowdin_prep.yml` /
  `crowdin_translate.yml` filenames and raises against the current
  sample's names. The placeholders here are already filled, so the
  script had no remaining use and would only mislead.

Plus two fixes for upstream bugs, both reported upstream and to be
dropped when the sample catches up:

- **`intl/initial_sync.py` placeholder regex.** The sample has
  `r'/_core_name_(?=[/.])]'` — a stray `]` after the lookahead, which
  asserts the next character is both `/`-or-`.` *and* `]`. The regex
  therefore matches nothing, the `_core_name_` placeholders survive into
  `crowdin.yaml`, and the initial upload lands in a Crowdin path
  literally named `_core_name_`. The reset path further down the same
  file got the identical tightening right, so it is plainly a typo.
  Older copies (nestopia) use a working `r'/_core_name_'`.
- **A `CROWDIN_API_KEY` guard was added as the first step of each
  workflow.** The vendored scripts call `subprocess.run()` without
  `check=True` throughout, so a failed Crowdin CLI invocation still
  exits 0 and the workflow step reports success. Without the guard, an
  unset secret produces a green run that did nothing at all. The guard
  is deliberately in *our* workflow files rather than as `check=True`
  edits across five upstream scripts: it covers the failure that
  actually happens, and it cannot be undone by the sync workflow's own
  `git add intl/*_workflow.py`.

The rest of `intl/` is upstream as-is; the sync workflow updates
`intl/*_workflow.py` itself.
