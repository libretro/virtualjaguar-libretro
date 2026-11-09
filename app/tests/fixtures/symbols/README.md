Real `rmac` / `rln` output for `app/tests/debug_symbols_test.cpp` (#1195), pinned by
`tools/jaguar-toolchain/PIN`. CI has no toolchain, so the outputs are committed. To regenerate:

```bash
rmac -fa symbols.s -o symbols.o
rln -e -s -a 4000 x x symbols.o -o globals.cof
rln -e -l -a 4000 x x symbols.o -o locals.cof
rln -e -a 4000 x x symbols.o -o stripped.cof
rln -s -l -a 4000 x x symbols.o -o symbols.abs
```
