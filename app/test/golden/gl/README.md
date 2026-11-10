# Golden screenshots for the GL qualification (second pass)

Layout: `app/test/golden/gl/<os>/<preset path>.png`, where `<os>` is `linux`, `windows` or
`macos` and `<preset path>` is the preset's path relative to the `shaders/` folder beside the
executable (`vj-scanlines.slangp`, `slang-shaders/crt/crt-royale.slangp`, ...).

Nothing is committed here yet. `gl-qualification.yml` compares a capture with its golden when
one exists and reports the mean absolute channel difference in the job summary without failing
(`gl_qualify.py --goldens`, `--goldens-strict` to gate). To produce a set, run the workflow with
the `refresh_goldens` input: every passing capture is uploaded in this layout inside the
`gl-qualification-<leg>` artifact under `golden/gl/<os>/`, for a human to review and commit.
llvmpipe (Linux) is deterministic across runs, so that leg is the one to gate first.
