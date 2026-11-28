# Frame scheduler (native port, piece 4)

Destiny's simulation runs on a fixed tick: 30 per second on the console. The PC port keeps a fixed
tick and raises its rate: one simulation tick per rendered frame at the `fps` setting, with no
interpolation between ticks and no tick that follows the frame time. Game logic runs at the frame
rate itself.

## How the console schedules a tick

| | |
|---|---|
| `00593f00` | builds the "time control config globals" (pointer at `026bcd08`): `+0x08` frame period in microseconds (33,366), `+0x38` integer tick rate (30) |
| `00593880` | returns `+0x38` |
| `009067f0` | builds the game options; their first field is the tick rate, from `00593880` |
| `009067e0` | returns a pointer to that field in the game globals (`0273dcd0 + 8`); 25 call sites read it |
| `005915c0` | sets the game-time globals (`026bc880`: `+4` ticks per second, `+8` seconds per tick) from it |
| `00591710` | per frame: turns the real elapsed time into ticks of `+0x08` each (at most about two per frame), and recomputes the game-time globals from the same integer when a time-scale effect (slow motion) runs |
| `00594460` | main loop; waits until the last frame + `+0x08` (`00594a3b`) |

## What the port does

`time_init_hook` (`native/src/hooks.cpp`) sets `+0x08` and `+0x38` once, after `00593f00` builds them:
period `1/fps`, rate `fps`. Everything above derives from those two values, so the tick rate, the
nominal tick and the frame pacing agree, and nothing changes per frame. Removed with it: the
per-frame rewrite of the tick rate from the frame's duration (the cause of the shaking in orbit and
the world), the unlimited-rate patch of the main loop's wait, and the research switches
`D1_TICK_RATE_FORCE`, `D1_TICK_NOMINAL30` and `D1_NATIVE_FIXED_TICK`.

A PC that cannot sustain the rate slows the game down, as the console does; it never skips time.
The default stays 30 until the systems below are fixed.

Checked on lavapipe: at 30 and 60 Hz the game reaches Earth patrol and renders correctly, and the
log reports the configured tick.

## Systems that assume 30 ticks a second

Code that scales by the tick length adapts by itself. What needs fixing is code that bakes 30 Hz
in: per-tick amounts, tick counts used as time, 30 Hz constants. Each confirmed case is fixed in the
decompiled function and recorded in `records/bugs.tsv` (kind `limit`).

Known:

- Camera kick (weapon recoil), `00900ad0`: applied per tick and scaled by tick / nominal tick, so at
  a higher fixed rate the full kick lands on every tick (measured earlier at 5x the recoil at 144
  ticks a second). `B0001`.

Candidates from a scan of the executable for floating-point constants: 15 uses of 1/30 in 11
functions, to be classified one by one:

- `001c57a0`
- `0052f130`
- `007f24d0`
- `007f3560`
- `007f9590`
- `0082f460`
- `0082f610`
- `0089b1f0`
- `008f8a10`
- `00af9890`
- `00e5d140`

The same scan finds 61 uses of 30.0 in 53 more functions; most are angles, distances and field of
view, so they are only checked where a function also handles time. Integer tick counts (a number of
ticks used as a duration) do not show up in a constant scan; they are found by running at 60 Hz
and following what runs at the wrong speed.
