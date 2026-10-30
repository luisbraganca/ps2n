# Controller input

Recompiled games take controller input the way PCSX2 does: every input of
the emulated DualShock 2 is bound to keys and game controller buttons or
axes, the bindings are edited in a settings window inside the game's window,
and they are kept in an INI file in PCSX2's format, so a PCSX2 user's
`[Pad1]` section carries over. This page describes what the runtime does; it
follows PCSX2's behaviour as documented from its source, but none of its code.

It needs a build with SDL2 (see [runtime.md](runtime.md#running-a-recompiled-game)).
Headless runs (`--no-window`, the tests) keep the default bindings, and
`--pad-script` replaces all of it with scripted input.

## Using it

- **Escape** opens the pause menu: Resume Game, Controller Settings, Toggle
  Fullscreen, Quit Game. The game is paused while it is open and sees no
  input; whatever was held when the menu closed (the Return that chose
  Resume Game) reaches the game only after it is released.
- **Controller Settings** has PCSX2's pages: Global Settings, Controller
  Port 1, Controller Port 2 and Hotkeys. It works with the mouse, the
  keyboard (arrows, Return, Escape) or a controller (d-pad or left stick,
  the bottom face button selects, the right one goes back).
- A **port page** has a Controller Type (DualShock 2 or Not Connected), a
  Bindings and a Settings tab, and Automatic Mapping and Clear Mapping. The
  bindings are laid out as in PCSX2: the d-pad and the left stick on the
  left, the face buttons and the right stick on the right, the shoulder
  buttons, Select, Start, L3, R3, Apply Pressure and Analog around a picture
  of the controller in the middle. The picture shows what the port reads
  from the host as it happens, and a binding's button lights while it is held.
- On a binding's button:
  - **Left click** binds anew: the button reads "Push Button/Axis... [5]"; press a key, a controller
    button, or push an axis past half way (several at once make a chord).
    The binding is made when one of them is released. A mouse click, or five seconds, cancels.
  - **Shift + left click**, or a click on an input with several bindings, opens Edit
    Bindings: the list, Add Binding, Remove Binding, Clear Bindings, and the
    input's own Sensitivity and Deadzone.
  - **Right click** clears the input's bindings.
  - The motor buttons (Large Motor, Small Motor) choose a controller's motor
    from a list instead.
- **Automatic Mapping** binds every input to a connected controller (or to
  the keyboard layout below) in one go; Apply Pressure and the settings stay
  as they were. **Clear Mapping** removes every binding of the port.
- Changes are saved at once.

## The settings file

`ps2n.ini`, found in this order:

1. `--settings FILE`;
2. `ps2n.ini` next to the game's executable, when it exists (a portable set-up);
3. the per-user file, shared by every recompiled game: `%APPDATA%\PS2Native\ps2n.ini`
   on Windows, `~/Library/Application Support/PS2Native/ps2n.ini` on macOS,
   `$XDG_CONFIG_HOME/PS2Native/ps2n.ini` (or `~/.config/PS2Native/ps2n.ini`) elsewhere.

It does not exist until a setting changes; until then the defaults apply.
Sections and keys are PCSX2's:

```ini
[Pad1]
Type = DualShock2
AxisScale = 1.33
Cross = Keyboard/K
Cross = SDL-0/FaceSouth
LUp = Keyboard/W
LUp = SDL-0/-LeftY
LargeMotor = SDL-0/LargeMotor

[Pad2]
Type = None

[Hotkeys]
ToggleFullscreen = Keyboard/Alt & Keyboard/Return
```

A key may repeat: each line is one more binding for that input. Sections the
runtime does not use are kept as they are when it writes the file.

### Bindings

| Form | Meaning |
|---|---|
| `Keyboard/<key>` | A key, by Qt's name for it, as PCSX2 writes them: `A`-`Z`, `0`-`9`, `F1`-`F24`, `Return`, `Escape`, `Space`, `Tab`, `Backspace`, `Up`/`Down`/`Left`/`Right`, `Shift`, `Control`, `Alt`, `Meta`, `Minus`, `Equal`, `Comma`, `Period`, `Slash`, `Semicolon`, `Apostrophe`, `QuoteLeft`, `BracketLeft`, `BracketRight`, `Backslash`, `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, and the keypad as `Numpad0`-`Numpad9`, `NumpadEnter`, `NumpadPlus`, ... Keys follow the keyboard layout (what the key types), not their position; left and right modifiers are one key. |
| `SDL-<n>/<button>` | A game controller button: `FaceSouth`, `FaceEast`, `FaceWest`, `FaceNorth` (by position: on a DualShock the cross, circle, square and triangle; PCSX2's older `A`, `B`, `X`, `Y` are read too), `Back`, `Guide`, `Start`, `LeftStick`, `RightStick`, `LeftShoulder`, `RightShoulder`, `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight`, `Misc1`, `Paddle1`-`Paddle4`, `Touchpad`. |
| `SDL-<n>/+<axis>`, `-<axis>` | One direction of an axis: `LeftX`, `LeftY`, `RightX`, `RightY` (down and right are `+`), `LeftTrigger`, `RightTrigger`. `Full<axis>` reads the whole range (-1 to 1 as 0 to 1); a `~` after the name reads it from the other end. |
| `SDL-<n>/LargeMotor`, `SmallMotor` | A controller's motors, for the `LargeMotor`/`SmallMotor` keys. |
| `<a> & <b>` | A chord: up to four inputs held together. |

`SDL-<n>` numbers controllers as PCSX2 does: a controller takes its SDL
player index when that is free (an XInput controller's slot), else the
lowest free number. Bindings of sources this runtime does not read
(`XInput-<n>`, `DInput-<n>`, `Pointer-0`, raw joysticks' `JoyButton<n>` and
the like) are kept in the file but do nothing.

### DualShock 2 inputs

`Up`, `Right`, `Down`, `Left`, `Triangle`, `Circle`, `Cross`, `Square`,
`Select`, `Start`, `L1`, `L2`, `R1`, `R2`, `L3`, `R3`, `Analog` (Analog
Toggle), `Pressure` (Apply Pressure), `LUp`, `LRight`, `LDown`, `LLeft`,
`RUp`, `RRight`, `RDown`, `RLeft`, and the motors `LargeMotor` and
`SmallMotor`. Each input may also have `<input>Scale` (its Sensitivity, 1 to
200%) and `<input>Deadzone` (0 to 100%), applied to each of its bindings.

### Port settings

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Type` | `DualShock2` on port 1, `None` on port 2 | `DualShock2`, `None` | Whether a controller is plugged in. Other PCSX2 types are read as `DualShock2`. |
| `InvertL`, `InvertR` | 0 | 0-3 | Turn a stick around: 1 left/right, 2 up/down, 3 both. |
| `Deadzone` | 0 | 0-1 | Radial stick deadzone, measured after the sensitivity. |
| `AxisScale` | 1.33 | 0.01-2 | Stick sensitivity. Above 1, a round stick reaches the corners of the square range a DualShock 2 reports. |
| `UseDiagonalScaleCorrection` | false | | Stretch a round stick's diagonals out to the corners (use with `AxisScale = 1`). |
| `LargeMotorScale`, `SmallMotorScale` | 1 | 0-2 | How strongly the game's vibration drives each motor. |
| `ButtonDeadzone` | 0 | 0-1 | Buttons and triggers read as released below this. |
| `PressureModifier` | 0.5 | 0.01-1 | The pressure buttons report while Apply Pressure is held. |

### What the game reads

- A key or button gives full pressure (255); with Apply Pressure held,
  `PressureModifier` of it. L2 and R2 keep their own value, so a trigger
  gives proportional pressure.
- A key on a stick direction gives full tilt; a controller axis is
  multiplied by `AxisScale`.
- When several bindings of one input are held, the strongest counts. A chord
  counts only while all of its inputs are held.
- The Analog button switches between digital and analog mode, unless the
  game locked the mode. A DualShock 2 starts digital, as the real one does.
- The game's vibration drives the motors bound to the port: the large motor
  at the game's speed, the small one on or off, each times its scale. Both
  on one controller motor: the stronger wins. It stops while the game is paused.

### Defaults

Port 1 starts with PCSX2's keyboard layout. PCSX2's first-run wizard offers
Automatic Mapping for a connected controller; here, as long as there is no
settings file, the first controller that connects is mapped to port 1 beside
the keyboard (each input gets both bindings), and the settings file is
written. After that, controllers are bound only by Automatic Mapping or by hand.

| Input | Key | | Input | Key |
|---|---|---|---|---|
| D-pad | arrows | | Left stick | `W` `A` `S` `D` |
| Triangle | `I` | | Right stick | `T` `F` `G` `H` |
| Circle | `L` | | L1, R1 | `Q`, `E` |
| Cross | `K` | | L2, R2 | `1`, `3` |
| Square | `J` | | L3, R3 | `2`, `4` |
| Start | `Return` | | Select | `Backspace` |

Automatic Mapping of a controller binds Cross, Circle, Square and Triangle to
the face buttons by position, Select to `Back`, Start to `Start`, Analog to
`Guide`, L1/R1 to the shoulder buttons, L2/R2 to the triggers, L3/R3 to the
stick clicks, the d-pad and both sticks to their own, and the motors to the
controller's motors.

### Hotkeys

| Key in `[Hotkeys]` | Category | Default |
|---|---|---|
| `OpenPauseMenu` | Navigation | `Escape` |
| `ToggleFullscreen` | Navigation | `Alt & Return`, and `F11` (an addition) |
| `TogglePause` | Speed | `Space` |
| `ToggleFrameLimit` | Speed | `F4` |
| `ToggleTurbo` | Speed | `Tab` |
| `HoldTurbo` | Speed | `Period` |
| `ShutdownVM` | System | none: quits the game |
| `CycleInterlaceMode` | Graphics | `F5`: Adaptive, Weave, Bob deinterlacing (see [runtime.md](runtime.md#running-a-recompiled-game)) |
| `Screenshot` | Graphics | `F8`: the picture as a BMP in `snaps/` under the working directory |
| `Mute` | Audio | none: Toggle Mute |
| `IncreaseVolume` | Audio | none: the volume up 5%, to 200% at most (see [runtime.md](runtime.md#running-a-recompiled-game) for the sound settings) |
| `DecreaseVolume` | Audio | none: the volume down 5% |

As in PCSX2, a hotkey acts when it is released, except Hold Turbo, which lasts
while it is held. The frame limiter and turbo both run the game as fast as
the host can (there is no speed setting yet). A chord that is held takes its
keys from everything else until each is released: Alt+Return toggles
fullscreen without pressing Start.

### Controller options

| Key | Default | Meaning |
|---|---|---|
| `[InputSources] SDLControllerEnhancedMode` | true | Drive the rumble and lights of DualShock 4 and DualSense controllers. |
| `[InputSources] SDLPS5PlayerLED` | true | Light a DualSense's player LEDs. |
| `[InputSources] SDLRawInput` | false | Windows: read controllers through raw input. |
| `[SDLExtra] Player0LED` ... `Player3LED` | `000080`, `800000`, `008000`, `808000` | Light bar colour of SDL-0 to SDL-3. |
| `[SDLHints]` | | Any SDL hint, `NAME = value`, applied after the ones above. |

A `game_controller_db.txt` next to the settings file adds SDL controller
mappings, as PCSX2's does. Controllers keep working while another window has
the focus.

## Not like PCSX2

- No input profiles, macros, multitap, mouse, or controller types other than
  the DualShock 2 yet; ports 3 to 8 are not read.
- PCSX2 lets every binding of an input overwrite the others (the last event
  wins) and lets each part of a chord press a pad input alone; here the
  strongest binding wins and a chord needs all of its parts.
- When both directions of a stick are held, PCSX2 favours the positive one;
  here they cancel out.
- PCSX2 scales each motor by the other motor's scale setting; here each
  motor uses its own.
