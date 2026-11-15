# AI Assistance Policy

## How this project uses AI

This project was **developed with the help of AI** — Claude (Anthropic) — as a cooperation. AI assisted with brainstorming, code generation, and documentation, but I reviewed all output, validated results against reference implementations, and take full responsibility for the work and any mistakes.

## Responsible use

- ✅ **I checked the results** — The machine runs HP's real 48GX ROM (revision R) on the PC: it boots, computes, keeps its state through a save and load, and its BEEP sounds at the right pitch (`make test`); every C47-layout key was run on the ROM and its screen checked (`make keys`)
- ✅ **I maintained domain expertise** — I guide the AI with the design decisions: the keyboard (the C47 overlay's legends doing the HP 48's functions of the same meaning, or the HP 48's own layout), the screen, and how it should feel on the calculator
- ✅ **I take full responsibility** — The work is mine; any errors are mine to fix
- ✅ **I'm transparent** — This file documents how AI was used

## Testing & Validation

- The HP 48GX ROM booted and used through its keyboard on the PC (`test/hptest.c`, `make test`)
- The C47 keys, one by one, against the ROM (`make keys`)
- Every screen checked in the PC simulator (`make shots`) before a commit
- To be tested on real DM42n hardware

## License

This does not affect the GPL-3.0-or-later license. All code remains open-source and fully accessible.

---

*For more details, see `LICENSE` and the README.*
