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
| [007: Agent Under Fire](#007-agent-under-fire) | Europe | SLES-50539 | In-game | 2026-11-04 |
| [Resident Evil Code: Veronica X](#resident-evil-code-veronica-x) | USA | SLUS-20184 | In-game | 2026-11-04 |

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

- The loader, the movie player, `ACTION.UK` and `DRIVING.ELF` run
  recompiled, and so does the VU1 micro code `ACTION.UK` uploads.
- The language selection, the intro movies, the copyright and title screens,
  the menus, and the first mission, Trouble in Paradise, played to its end.
- The second mission, a driving mission (`mis02a`) that `DRIVING.ELF` runs:
  its opening movie and its race, with their textures.
- Sound in the movies, the menus, the first mission and the second mission's
  race. Since VU1 got a thread of its own, play tests hear it working
  properly (2026-11-04): the robotic voices in the race and the faint
  artifacts heard now and then in the first mission are gone. A counter of
  sudden jumps in the sound still finds 9 to 11 a second in the first
  mission's views measured, as many in deterministic runs as in real time,
  so they do not come from the game's thread falling behind.
- Controller input, from the keyboard or a game controller.
- 16:9, with PCSX2's widescreen patch for this disc: the `Widescreen 16:9`
  group of `SLES-50539_992B46DC.pnach` (by Arapapa and Silent, in PCSX2's
  `resources/patches.zip`), named in the boot config's `[pnach]`
  ([config.md](config.md#pnach-pcsx2s-patch-files)). Its lines change
  constants in the cameras' code, one word of `ACTION.UK` and four of
  `DRIVING.ELF`, under conditions that pick the language's executable; the
  loader and `MOVIE.ELF` get none. The window shows 16:9 under Auto. As in
  PCSX2, the menus and the HUD stretch with the picture, and the films do too
  unless FMV Aspect Ratio Override is Standard (4:3).

### Known problems

- In the first mission's heaviest views drawing falls behind, and the game
  shows fewer than 50 pictures a second (see [Speed](#speed)).
- The driving missions read GS memory back through VIF1 about once a frame
  (reported as `VIF1 to-memory transfer`), which the runtime does not do yet:
  an 8x8 block of the depth buffer (`PSMZ32`) at a point that drifts with the
  view, most likely to tell whether the sun is hidden before its glare is
  drawn. What the glare does without it has not been checked.
- `DRIVING.ELF`'s VU1 micro code runs on the interpreter (in real time on
  VU1's own thread). It uploads its programs in many combinations, over 5,000
  distinct micro memories in the mission's first two minutes, and they have
  not been recorded for the VU recompiler.
- In real time the second mission's race slows to 46 vblanks a second at the
  end of its heaviest stretch measured (see [Speed](#speed)).
- The second mission has been played into its race; how far it can be
  played has not been tried.
- With the widescreen patch, the race's camera takes 1.817 for its width
  over height, where 16:9 is 1.778. The patch writes `0x3FE88E39` over the
  game's 4/3 (`0x3FAAAAAA`, at `0x0012B7D0`); 16/9 is `0x3FE38E39`, whose
  lower half the patch has, so one digit looks mistyped. The value goes to
  the race camera's projection, which should then draw everything about 2%
  narrower than 16:9 needs (not measured), as in PCSX2, which applies the
  same file.

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
helicopter cutscene that opens the mission 49. Measured again on November 4,
the heaviest view showed 27 pictures a second, at 50 vblanks a second, with
VU1 on its own thread or not; why it is down from 39 has not been looked
into.

The second mission's race runs at 49.4 vblanks a second in real time over
its heaviest stretch measured (vblanks 6,800 to 9,000 of a scripted run that
starts it in place of the first mission): 50 for most of it, 46 at its end.
VU1 runs the mission's micro code on its own thread, on the interpreter.

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
- **The EE's work held to the clock.** The game's waits for the GS time out
  after a fixed number of polls. In real time they still ran out early, the
  recompiled loop polling far faster than the EE, and the game reset its
  drawing: a room in the first mission flickered, at 35 to 47 pictures a
  second. With the EE's work held to the clock it shows 50.
- **The SPU2 waits for the IOP's answer.** The sound driver mixes into the
  halves of a looping buffer, and libsd arms the next half when the SPU2's
  interrupt says one has played. When the SPU2 had fallen behind, it stopped
  at that interrupt, but arming the next half first brought it up to the
  clock: it played the backlog before the half was armed, and repeated old
  blocks. In real time the driver got 82 halves a second facing the lobby of
  the first building and 90 outdoors, where it should get 93.75, and a run
  counted 74 and 27 breaks a second in the sound (jumps well above the level
  around them). Now the driver gets 93.75 halves a second in both, and the
  breaks fall to 24 and 8.4 a second. A deterministic run of the mission's
  opening counts 8.5.
- **Interrupts charge only the work done.** An interrupt cuts the EE's
  current slice of back-edges short, and each slice was charged whole. The
  second mission's race takes thousands of VIF and GIF interrupts a second,
  and in real time the extra charge held it to 29 to 32 vblanks a second in
  its heaviest stretch. Charging only the back-edges that ran, the same
  stretch runs at 38 to 46. The sound there still breaks up about as often.
- **VU1 on a thread of its own.** In real time VU1 runs its micro programs on
  a host thread of its own, the game's thread only queuing them. Measured on
  one build, with and without `--no-vu1-thread`: the second mission's race
  runs at 49.4 vblanks a second instead of 39.2 over vblanks 6,800 to 9,000,
  and its sound breaks 0.9 times a second instead of 38 (the repeated blocks
  that made voices sound robotic). Facing the lobby of the first building
  the sound breaks 9.9 times a second instead of 29.9 in the lighter view,
  and 10.5 instead of 55.6 in the heaviest, as many as deterministic runs of
  the same views count; vblanks stay at 50, and pictures are the same either
  way (50 and 27 a second). The first mission's opening is unchanged: 49.9
  vblanks a second, and 9.0 breaks a second instead of 9.2. Deterministic
  runs draw the same frames as before.

## Resident Evil Code: Veronica X

The USA release, an NTSC game: it needs no `--region`. It is one executable,
`SLUS_201.84`, built with Metrowerks CodeWarrior and with its symbols, so its
config (`cvx.toml`) names little more than the ELF. Code and data share the
executable's one section, which the recompiler splits at the first data
object ([config.md](config.md#where-the-code-is)). The game reboots the IOP
with `IOPRP213.IMG`, and Sony's CSL sound modules (`modhsyn`, `modmidi`,
`modmsin`) and Tamsoft's sound driver (`TSNDDRV.IRX`) run on the IOP
interpreter. PCSX2 rates this release Perfect, with no game fixes.

### What runs

- The executable runs recompiled; no EE code has needed the interpreter. Its
  VU micro code runs on the VU interpreter: it has not been recorded for the
  VU recompiler.
- The memory card check, the content warning, CRI's ADX logo, the Capcom
  logo, the opening movie, the title screen and its menu, the load screen
  (with no save data), and a new game: the prologue, the long opening movie,
  and the first moments in the game proper, where the status screen opens on
  the Playing Manual.
- The first areas, played in real time with a controller: walking, fights,
  doors, picking items up and saving at a typewriter. The status screen with
  its item and file views, and the cutscene where Rodrigo frees Claire.
  Since the triangle fix under [What it took](#what-it-took), play tests see
  the status screen's text, the item models and Rodrigo drawn without the
  artifacts they had.
- Sound and music in the menus and the movies.
- Controller input.
- 16:9, with PCSX2's widescreen patch for this disc: the `Widescreen 16:9`
  group of `SLUS-20184_24036809.pnach` (by Gabominated, ported from
  ElHecht's patch for the European release, in PCSX2's
  `resources/patches.zip`), named in the boot config's `[pnach]`
  ([config.md](config.md#pnach-pcsx2s-patch-files)). Its three words are in
  BSS, which the game fills at run time, so the runtime writes them at
  every vblank: the camera matrix's horizontal scale (`crmat`, 1.0 becomes
  0.75), and the films' display size (`mdSize`: 640 by 320 at line 64
  becomes 640 by 426 at line 12, which the 16:9 window stretches back to
  the film's shape). The window shows 16:9 under Auto. The status screen
  and the other menus are drawn for 4:3 and come out stretched, as in
  PCSX2.

### What it took

- The recompiler splits a section that holds both code and data where the
  symbols say the data begins. Before, its 400 KB of data were translated
  as code, with 11,517 unknown instructions and 924 unresolved indirect
  jumps; 101 remain, calls through tables of functions, which the dispatcher
  handles.
- PADMAN 4 keeps the controller's button mask in libpad's data area, where
  Sony's libpad reads it before it enters pressure mode. Without it the
  game's pad set-up never finished, and START did nothing.
- A module's own RPC server takes the binds from the runtime's stand-in for
  the same sid: the Tamsoft driver serves SDRDRV's.
- `SetVSyncFlag` writes its flag once. libkernel's `VSync2` registers two of
  its locals, and writing them at every vblank corrupted a saved register in
  whatever stack frame took their place: the title screen loaded its data
  ten times over, then exited with "This texture is not adequate size!!".
  Interrupt handlers and alarms also run with the `$gp` they were registered
  with, as on the console.
- In real time, a short wait yields instead of sleeping for a timer tick, and
  a bind to a sid nobody serves waits five seconds, not half a second, for a
  module to register it. Before, the sound driver's upload of its samples
  took a tick per kilobyte, the game's bind to the driver gave up, and the
  game hung at start-up.
- The GS interpolates a triangle's colours and texture coordinates from its
  first vertex by the other two's weights, so a value its three vertices
  share comes out exact ([architecture.md](architecture.md#pixels)). The
  game draws text, item models and characters with an alpha of 128 behind
  an alpha test of 128 or more; weighing all three vertices under the EE's
  rounding toward zero gave 127 at scattered pixels, which failed it. The
  status screen's letters came out hollow (sprites drawn after the text, at
  a lower depth, covered every pixel that had failed), its item and file
  models speckled, with blue streaks behind them, and Rodrigo's arms partly
  missing, in the cutscenes and in play.

### Known problems

- Only the first areas have been played (2026-11-04).
- The game's own code turns a centred left stick into UP in one of its input
  words (its test for an upward push compares the wrong way round), as it
  would on a console. Neither the menus nor play go by that word: with the
  stick untouched, Claire stands still.

### Speed

Measured on the same Intel i5-14600KF. In real time the game runs at full
speed, 59.8 vblanks a second (NTSC is 59.94), through the first 7,200 vblanks
of a headless scripted run: start-up, the logos, the movies, the title
screen and its menu, and the start of a new game. The game proper has not
been measured in real time. A deterministic run (`--virtual-clock`) takes
about three minutes from start-up to the first moments in the game proper,
21,000 vblanks.

The explosion in the graveyard, before the first zombies, slowed real-time
play down (2026-11-04). The game draws a picture every second vblank; there
it draws up to 4.5 million pixels a picture (15 times the screen), most of
them in about 40 fire triangles of up to 300,000 pixels each, from 4-bit
textures whose CLUT sits in the last blocks of GS memory. The draw threads
took the CLUT for a page long, as if it wrapped round the end of memory, so
those triangles went to one thread. With the CLUT's real extent, a
deterministic run from the save in front of the prison (8 draw threads) takes
about 50 ms a picture at the explosion's height instead of 150 (25 ms
elsewhere); every dumped frame is the same as before. Still drawn by one
thread: the full-screen copy of the picture the effect draws from (its
texture is the other frame buffer, declared 512 lines high) and primitives
whose CLUT lies in the depth buffer's memory.
