# Games

How far each game gets, what still goes wrong and how fast it runs. A game is
listed once it has been tried. Stages follow PCSX2's compatibility scale, and
the README's [Games playable](../README.md#by-area) bar counts the games at
Playable or Perfect.

| Stage | The game |
|---|---|
| Nothing | does not boot, or shows nothing |
| Intro | shows its intro, such as logos and movies, but does not reach the menus |
| Menus | reaches the menus, but not the game proper |
| In-game | gets into the game proper, with major problems or parts that do not work yet |
| Playable | can be played from start to finish, with minor problems |
| Perfect | plays as it does on the console |

| Game | Region | Serial | Stage | Last tested |
|---|---|---|---|---|
| [007: Agent Under Fire](#007-agent-under-fire) | Europe | SLES-50539 | In-game | 2026-11-03 |

To bring up another game, start with the README's
[Porting a game](../README.md#porting-a-game). Then give the game a section
here, with the same parts as the one below.

## 007: Agent Under Fire

A PAL game: run it with `--region europe`. Its disc holds a loader
(`SLES_505.39`), a movie player (`MOVIE.ELF`), the game proper as one
executable per language (`ACTION.UK` is the English one), and the driving
missions (`DRIVING.ELF`). The loader is the program the disc boots, so its
config lists the other executables under `[input] programs`, as in
[config.md](config.md#games-with-several-executables).

### What runs

- The loader, the movie player and `ACTION.UK` run recompiled, and so does the
  VU1 micro code they upload.
- The language selection, the intro movies, the copyright and title screens,
  the menus, and the first mission, Trouble in Paradise, as far as it has been
  played.
- Sound in the movies and the menus.
- Controller input, from the keyboard or a game controller.

### Known problems

- In the first mission the sound breaks up.
- In the mission's heaviest views drawing falls behind, and the game shows
  fewer than 50 pictures a second (see [Speed](#speed)).
- Play tests have heard a glitch in the music on the copyright screen that
  recordings of it (`--dump-audio`) do not have. The cause has not been found.
- The driving missions' executable is not recompiled yet.
- Nothing past the first mission has been tried.

### Speed

Measured on an Intel i5-14600KF (14 cores) under Windows. Vblanks a second
say how fast the emulated console runs: 50 is full speed for this PAL game.
Pictures a second count the new pictures the game finishes, which fall below
the vblank rate when drawing cannot keep up. Real-time runs follow the host
clock, as in play. Deterministic runs (`--virtual-clock`) draw the same frames
on every run, so they show whether a change kept every frame as it was.

The movies, the copyright and title screens and the menus run at full speed,
50 vblanks a second in real time. The first mission does too, but drawing
falls behind in its heaviest views. Facing the lobby of the mission's first
building, the heaviest view measured, the game shows 39 pictures a second at
50 vblanks a second, with its VU1 micro code recompiled. With the micro code
interpreted, the game itself slows to 46 vblanks a second there, and drawing
keeps up. A lighter view in the building shows 50 pictures a second, and the
helicopter cutscene that opens the mission 49.

#### What each change bought

In the order the changes were made:

- **A safepoint on every backward jump.** The game waits for the GS in a
  polling loop that the recompiler had split across two functions, and a
  backward jump into another function was not a safepoint. The interrupt
  handler that ends the wait never ran inside the loop, so the wait timed out
  on every frame, and the main menu ran at 3 to 5 vblanks a second in a
  deterministic run.
- **Large primitives drawn on 8 threads.** Before, the title screen dropped to
  14.5 vblanks a second.
- **The GS's own thread.** Drawing used to run inside the EE's DMA kicks. The
  scene the game redraws behind its copyright screen on every vsync took
  longer than a frame, so the game's own thread never ran: real-time runs
  stayed on that screen at about 46 vblanks a second, the sound repeating a
  512-sample block 8,516 times in 110 s. With the GS on its own thread, the
  movies, the copyright and title screens and the main menu held 50 vblanks a
  second throughout three headless real-time runs of 150 to 200 s each. The
  movies went from 49 to 50 as well: each vblank is scheduled from when the
  previous one was handled, and the guest thread now handles them on time.
- **The draw threads share every primitive.** They had shared only large ones,
  so the first mission's thousands of small triangles were drawn on one
  thread, which held the mission to 28 to 31 vblanks a second. Sharing every
  primitive brought it to 50 in a headless real-time run, and a deterministic
  run of the mission from 14.4 to 41.9 vblanks a second.
- **A real-time vblank waits for the game's threads.** Inside the first
  mission's building the game redraws each field from its vsync handler.
  Facing the lobby, the vector units' interpreted work for it took about
  40 ms a field: the game's threads never ran again, and the picture and the
  sound froze. A vblank now waits until the threads have had half a frame over
  the last two frames, so the game slows down there instead.
- **A faster VU interpreter.** It decodes each instruction pair once, and
  looks register hazards up in a table instead of searching the results in
  flight. Facing the lobby the game holds 45 to 49 vblanks a second in real
  time, against 27 to 32 before, on the same idle machine. Deterministic runs
  gain less, the guest thread decoding the GS there too: from 22 to 26
  vblanks a second facing the lobby, and from 25 to 27 in the mission's
  opening.
- **Recompiled VU micro code**, from the micro code a run recorded
  ([`[vu] code_files`](config.md#vu-recompile-micro-code)). On a VU1
  transform loop the recompiled code is 3.1 times as fast as the interpreter.
  Inside the building a deterministic run went from 29.6 to 33.0 vblanks a
  second, with the same frames, sound and output. In real time drawing was
  the limit there: facing the lobby the game showed 41 to 42 pictures a second
  either way, at about 50 vblanks a second. Doubling the time the drawing
  takes cost 41% of them, and doubling the time the vector units take 4%. In
  the helicopter cutscene, recompiled micro code raised the pictures from 38
  to 40 a second, and the guest then waited for the GS thread, which may only
  be two vsyncs behind: vblanks came at 42 a second instead of 46. Those lobby
  runs turned the camera in real time, a step a picture, so builds that drew
  at different rates ended up facing different ways.
- **Faster drawing per pixel**, without changing a pixel. Each texture format
  samples through its own code, with each level's addressing and the CLUT's
  layout worked out with the drawing state, as are the frame and depth
  buffers' addressing. The depth test comes before texturing, and triangles
  work out eight pixels' weights at a time. With every primitive drawn on one
  thread, the deterministic run into the first building takes 357 s instead
  of 577, with the same frames and sound; with the draw threads, 131 s
  instead of 165 (vblanks 7,000 to 9,000 at 38.5 a second instead of 30). In
  real time, with the camera turned on the virtual clock first so that every
  build faces the same way, the game shows 46 pictures a second instead of 25
  in the heaviest view facing the lobby, at 46 vblanks a second (the
  interpreted micro code holds the guest back there), and 49 instead of 39 in
  a lighter one; in the helicopter cutscene it shows 48 instead of 38, with
  vblanks back at 50. With the micro code recompiled too, vblanks stay at 50
  in the heaviest view and drawing allows 39 pictures a second; the lighter
  view shows 50, and the helicopter cutscene 49.
