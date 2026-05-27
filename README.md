Wristquake
==========
*(a Pebble Time 2 fork of [`ultimate.text.watch`](https://github.com/zecoj/ultimate.text.watch))*

A minimalist text-only watchface that tells the time in words, with a shake-to-reveal status overlay.

## What Wristquake adds

- **Pebble Time 2 (emery) support.** Builds for all five platforms: aplite, basalt, chalk, diorite, emery. Status bars and text animations now scale to whatever screen size they're running on, instead of the upstream's hardcoded 144×168.
- **Step count on shake.** Today's step total appears alongside the date and battery in the shake-revealed status row (requires Pebble Health — basalt and later).
- **Bluetooth indicator works on color screens.** A small "BT" tag in the top-right corner replaces the upstream's `InverterLayer` trick, which the SDK removed for color watches.

## Features (inherited from upstream)

- Tells time in plain English three ways:
  - fuzzy (12:44 → "quarter to one")
  - human (12:44 → "sixteen to one")
  - machine (12:44 → "twelve forty four")
- Optional left/center/right alignment
- Shake reveals: time · weather, date · steps · battery%
- Midday / midnight handling
- Bluetooth disconnect vibrates and shows the "BT" indicator

## Building

Requires the [Pebble SDK](https://developer.repebble.com/sdk/) and `pebble-tool`.

```
pebble build
pebble install --emulator emery
```

Swap `emery` for `basalt`, `chalk`, `diorite`, or `aplite` to test the others.

## Known limitations / follow-ups

- **Chalk (round 180×180).** Builds but the rectangular layout isn't tuned for the round bezel — long lines may clip. Fix is a small refactor with `PBL_ROUND` insets.
- **No in-app config for `show_steps`.** The upstream config page lives at `zecoj.github.io/ultimate.text.watch/` and has no checkbox for steps, so the JS defaults the appKey to on. Migrating to [Clay](https://github.com/pebble/clay) would give us a local config UI with a checkbox; deferred until needed.

## Credits

- Upstream: [`zecoj/ultimate.text.watch`](https://github.com/zecoj/ultimate.text.watch) by Ben Nguyen
- Upstream's lineage: [Fuzzy Text International](https://github.com/hallettj/Fuzzy-Text-International) and [TextWatch-en](https://github.com/arska/TextWatch-en)
