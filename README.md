# sigma-chess-uci

A UCI chess engine ported to C from Sigma Chess by Ole K. Christensen
(1991–2011, originally PowerPC assembly and C). Single-threaded, with
optional Syzygy endgame tablebase support.

## Build

Needs CMake 3.10+ and a C11 compiler (Clang or GCC).

```
make
```

The engine is built as `build/sigma-uci`. Add it to any UCI chess GUI.

## License

BSD 2-clause; see `LICENSE`. The bundled Fathom tablebase prober in
`src/Syzygy/fathom` is MIT-licensed; see its `LICENSE`.
