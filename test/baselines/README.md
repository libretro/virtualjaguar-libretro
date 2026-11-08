# Regression screenshots

`regression_test.sh` compares the last screenshot dumped during its 600-frame
run (frame 500) against these images.

The Jagniccc baseline includes the live PIT1 timing readout introduced by
#1047. Its source (`JaguarDemos/jagniccc2000/jagniccc.S`, `again` loop) subtracts
PIT1 reads to display rendering and elapsed milliseconds. The old baseline
showed zeros because reads returned the timer reload value. The update changes
86 pixels within `(80,28)`–`(211,34)`; the scene and frame counter are unchanged.
Linux ARM64 and macOS ARM64 CI captures were pixel-identical.

Both baselines were re-captured for #1191, which widened the frame to TOM's
full unblanked line (HBE..HBB): yarc went from 326 to 352 px and jagniccc from
434 to 469 px. The previous images match the new ones pixel for pixel at x+16
(yarc) and x+21 (jagniccc). The new left columns show yarc's gradient and
on-screen labels. The other new columns are black. macOS ARM64, Linux ARM64
and Linux x64 CI captures were byte-identical.
