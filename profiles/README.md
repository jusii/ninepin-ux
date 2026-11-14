# pad profiles

Ready-made pad profiles for Ninepin UX. On the board's page, under Pad button combos: Add from a
file, choose one of these, Add profile. Then Load it. The format is described in
[the user guide](../doc/user-guide.md#profile-files).

Buttons are named by their place on the pad: `south` is ✕ on a PlayStation pad, A on an Xbox pad.
Keys reach an Amiga, a C64 Ultimate and a PC, not an original C64.

| file | machine | what |
|---|---|---|
| `jump-button.txt` | any | ✕ jumps (up), □ fires: platform games that jump with up |
| `pinball-dreams.txt` | Amiga | L1 / R1 flippers (the Shift keys), ✕ pulls the plunger (cursor down) |
| `turrican-2.txt` | Amiga | L1 power lines (Space), R1 gyroscope (down + Space), R2 super weapon (fire + Space) |
| `blue-max.txt` | C64 | R1 drops a bomb (down + fire), L2 Space |
| `commando.txt` | C64 | ○ throws a grenade (Space) |
| `turrican.txt` | C64 | L1 energy lines (Space), R1 grenade (F7), L2 gyroscope (down + Space) |

The keys are from the games' documentation, not all tried on the machines. Corrections welcome.
