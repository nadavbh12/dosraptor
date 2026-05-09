# DOS Raptor
This is the original Raptor Call Of The Shadows DOS source code v1.2 with the Apogee Sound System and the
DMX wrapper APODMX instead of the proprietary DMX library.

## Build
To build all libraries and the exe under DOS use Watcom C 10.0 and TASM 3.1.
For a build that more closely matches the original exe file v1.2 (without DMX library) you will need the following:
```
AUDIOLIB: Watcom C 10.0 and TASM 3.1
APODMX: A compatible version of Watcom C32 
GFX: Watcom C 9.5b and TASM 3.1
SOURCE: Watcom C 10.0 and TASM 3.1
```

## Runtime Data Files

Only source code is in this repository. To actually run the game you need a few
binary files alongside the executable (the game opens them by relative path, so
launch with the repo root as your working directory):

- **`FILE0000.GLB`** (~575 KB) and **`FILE0001.GLB`** (~4 MB)
  Encrypted asset archives — graphics, levels, sounds, MIDI music. These ship
  with the game and are *not* in this repository.

  - **Shareware (Episode 1: Bravo Sector)** was released as freeware by Apogee
    in 1994 and is freely redistributable. The most reliable mirror is the
    [Internet Archive](https://archive.org/) — search for
    "Raptor Call of the Shadows shareware". `RAPTOR.ZIP` / `1raptor.zip` are
    the canonical filenames. Extract `FILE0000.GLB` and `FILE0001.GLB` into
    the repo root.
  - **Registered version (Episodes 2 and 3 included)** is sold on
    [GOG](https://www.gog.com/game/raptor_call_of_the_shadows_2010_edition)
    and Steam as the "2010 Edition". Both bundle the original `.GLB` files
    inside the install directory (look for them next to the modern launcher).

- **`TimGM6mb.sf2`** (~6 MB)
  General MIDI SoundFont used by the port for music playback through FluidSynth.
  TimGM6mb is GPL-licensed and widely mirrored — search for `TimGM6mb.sf2`, or
  drop in any other `.sf2` you prefer and edit `SETUP.INI`'s `SoundFont=` line.

`CHAR*.FIL` save files are created next to the binary at runtime; you don't
need to provide them.

## License
DOS Raptor is distributed under the GPL Version 2 or newer, see [LICENSE](https://github.com/skynettx/dosraptor/blob/master/LICENSE).

## Thanks
All my thanks go to [Scott Host](https://www.mking.com), [nukeykt](https://github.com/nukeykt) and [NY00123](https://github.com/NY00123) without them this release would not have been possible.
