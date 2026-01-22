# Headless TilEm + zkeme80 workflow

This document captures the current headless workflow for building the
zkeme80 ROM, running it under TilEm, and debugging input/stack issues
with the trace tools.

## Build the zkeme80 flake

From the repo root, the zkeme80 sources live in `zkeme80-src/`.

1) Build the ROM with Nix:

```
cd zkeme80-src
nix build .#zkeme80
```

2) Outputs:

- ROM image: `zkeme80-src/result/zkeme80.rom`
- Label map (RAM symbols): `zkeme80-src/result/zkeme80.ram-labelmap.json`

The label map is used by the trace tooling to resolve Forth words and
variables (for example `var-sp0` and `LATEST`).

## How the ROM is written

zkeme80 is a Forth system built on Z80 assembly emitted by Scheme code.
The core pieces:

- `zkeme80-src/src/forth.scm`
  - Defines the Forth VM and primitives in Scheme S-expressions that
    emit Z80 assembly (`defcode`, `defword`, `dw`, `label`, etc.).
  - Forth words and their dictionary entries are constructed here.
  - The character lookup table lives here (`char-lookup-table`).
- `zkeme80-src/src/keyboard.scm`
  - Z80 keyboard scanning logic (`get-key`, `wait-key`, `flush-keys`).
- `zkeme80-src/src/bootstrap-flash*.fs`
  - Forth source for the shell and bootstrap words.
  - The shell prompt in this repo is driven by
    `zkeme80-src/src/bootstrap-flash5.fs`.

The Scheme sources are compiled into ROM data via `build.scm` and the
flake build pipeline.

## Build TilEm

From the repo root:

```
nix build
```

The built GUI binary is at `./result/bin/tilem2`. This binary supports
headless mode with `--headless` and can run macro scripts. There is
also a separate headless build under `headless/` (see `headless/Makefile`)
if you want a smaller binary without GTK dependencies.

## Headless mode basics

Run the GUI binary in headless mode, load the ROM, and run a macro:

```
./result/bin/tilem2 \
  --headless \
  --rom zkeme80-src/result/zkeme80.rom \
  --macro headless-echo-hey.macro \
  --headless-record headless-echo-hey.gif
```

Useful options:

- `--headless-record FILE.gif` records a GIF of the LCD during the run.
- `--macro FILE` runs a headless macro script (see below).
- `--trace FILE` writes a full instruction trace.
- `--trace-backtrace FILE` writes a ring-buffer trace (default 1GB).
- `--trace-range ram|all|0x8000-0xBFFF` selects a logical address range.
- `--trace-limit BYTES` caps trace size (default 500GB for full trace).
- `--trace-backtrace-limit BYTES` caps ring size (default 1GB).

## Headless macro scripting language

The headless script parser is implemented in `headless/script.c`.
Syntax is one command per line, with `#` or `//` comments. Time values
accept `s` or `ms` suffixes.

Commands:

- `wait <time>` (aliases: `sleep`, `pause`)
- `set key_hold <time>`
- `set key_delay <time>`
- `key <NAME> [hold <time>]`
- `press <NAME>` / `release <NAME>`
- `scancode <hex|dec> [hold <time>]` (aliases: `keycode`, `rawkey`)
- `type <text>` or `type "..."`
- `scanstring <text>` or `scanstring "..."`
- `screenshot <path.png>`
- `memdump <path> [mem|all|rom|ram|ram-logical|lram|lcd]`

### Key naming

`key` and `press/release` accept names like `ON`, `ENTER`, `LEFT`,
`RIGHT`, `UP`, `DOWN`, `ALPHA`, `2ND`, and number keys `0-9`.

`scancode` sends the raw key value used by the zkeme80 char lookup table
(`char-lookup-table` in `zkeme80-src/src/forth.scm`). This is the most
reliable way to enter Forth text because `type` uses the calculator's
ALPHA key map, which does not match the zkeme80 Forth map.

`scanstring` sends raw scan codes using the zkeme80 char lookup table
directly, so you can type Forth input as readable ASCII (including
digits and punctuation) without hand-writing `scancode` lines. It uses
`key_hold` and `key_delay` between characters.

`scanstring` sends raw scan codes using the ZKME80 char lookup table
directly, so you can type Forth input as readable ASCII (including
digits and punctuation) without hand-writing `scancode` lines. It uses
`key_hold` and `key_delay` between characters.

Example: in the Forth table, `H` is keycode 0x0E (decimal 14), while the
ALPHA map would produce `F` for that scan.

### Example macro

```
set key_hold 0.25s
set key_delay 0.05s
key ON
wait 7s
key RIGHT
wait 0.3s
key ENTER
wait 0.5s
scancode 14
wait 0.05s
scancode 14
wait 0.05s
key ENTER
wait 0.6s
memdump echo-hh.ram ram-logical
```

## Timing guidance (tested)

These values have been reliable in headless tests:

- `key_hold 0.25s` for manual key taps (especially ON and navigation).
- `key_delay 0.05s` when sending raw scancodes for text input.
- `wait 7s` after `key ON` to allow zkeme80 to boot and show the shell.
- `wait 0.2s - 0.3s` between menu/navigation key presses.

You can speed this up, but going below ~0.05s for key delay or hold can
result in missed keys.

## Trace and debugging workflow

### Full trace vs backtrace

- `--trace` writes every instruction to disk (very large).
- `--trace-backtrace` keeps a ring buffer in memory and writes it on
  exit (default 1GB). Use this when you need the most recent history
  leading up to a failure.

The default trace range is RAM (`0x8000-0xFFFF`). Use `--trace-range all`
if you need ROM or IO instruction coverage.

### Tools: tools/tilem_trace.py

`tools/tilem_trace.py` decodes trace files and can reconstruct RAM, show
keys, and interpret Forth word calls.

Common uses:

- Reconstruct RAM after the trace:

```
python3 tools/tilem_trace.py trace.bin \
  --dump-ram final.ram
```

- Reconstruct RAM at a specific instruction index:

```
python3 tools/tilem_trace.py trace.bin \
  --dump-ram snap.ram \
  --at 123456
```

- Print key press/release events:

```
python3 tools/tilem_trace.py trace.bin --print-keys
```

- Forth word trace with call stacks:

```
python3 tools/tilem_trace.py trace.bin \
  --labelmap zkeme80-src/result/zkeme80.ram-labelmap.json \
  --forth-rom zkeme80-src/result/zkeme80.rom \
  --forth-trace forth-trace.txt
```

- Detect DROP on empty stack (uses SP0 from the label map):

```
python3 tools/tilem_trace.py trace.bin \
  --labelmap zkeme80-src/result/zkeme80.ram-labelmap.json \
  --forth-rom zkeme80-src/result/zkeme80.rom \
  --forth-drop-underflow
```

- For ring backtraces, add `--resync` to skip partial records:

```
python3 tools/tilem_trace.py backtrace.bin --resync --print-flow
```

### Debugging EXPECT/WORD problems

- The shell uses `EXPECT` from `zkeme80-src/src/forth.scm` via
  `SHELL-BUF 128 EXPECT` in `zkeme80-src/src/bootstrap-flash5.fs`.
- If input handling breaks, trace for:
  - Stack underflow at `DROP`
  - The `expect-got-backspace` path
  - Buffer full behavior in `expect-full`

You can combine Forth tracing with key events to verify how the input
loop reacts to each keypress.

## Where to look for behavior

- Input stack discipline: `zkeme80-src/src/forth.scm` (`EXPECT`, `WORD`).
- Keyboard scan codes: `zkeme80-src/src/keyboard.scm` and
  `char-lookup-table` in `zkeme80-src/src/forth.scm`.
- Shell prompt behavior: `zkeme80-src/src/bootstrap-flash5.fs`.
- Trace format and capture: `headless/trace.c`.
- Macro script engine: `headless/script.c`.

## Quick reference commands

Build ROM:

```
cd zkeme80-src
nix build .#zkeme80
```

Run headless macro:

```
./result/bin/tilem2 --headless \
  --rom zkeme80-src/result/zkeme80.rom \
  --macro headless-echo-sentence.macro \
  --headless-record headless-echo-sentence.gif
```

Run with trace backtrace:

```
./result/bin/tilem2 --headless \
  --rom zkeme80-src/result/zkeme80.rom \
  --macro headless-echo-sentence.macro \
  --trace-backtrace backtrace.bin \
  --trace-backtrace-limit 67108864
```
