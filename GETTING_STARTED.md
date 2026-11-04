# Getting started

This guide takes you from a fresh computer to playing a PlayStation 2 game
through PS2Native, and to telling the developer what happened. It is written
for testers: every step is a command to copy into a terminal, and none needs
programming.

PS2Native is not an emulator that plays a disc. It translates the game's
program into C++ source code, and your computer compiles that into a program
of its own, which plays the game. So you build PS2Native once, make an image
of your game disc, recompile the game, build it, and play.

The project is in early development. No game can be played from start to
finish yet, and every game shows problems: finding them is what testing is
for.

The commands are for Linux; [On Windows](#on-windows) lists the ones that
differ there.

> **Linux is new.** PS2Native is developed on Windows and has not been built
> on Linux before, so these steps have not been tried there yet. If a step
> fails, stop and send its output (see
> [When something goes wrong](#when-something-goes-wrong)).

## What you need

- A 64-bit PC (Intel or AMD) with a recent Linux: Ubuntu 24.04 or later,
  Linux Mint 22 or later, Debian 13, Fedora 41 or later, or Arch. Windows 10
  and 11 work too.
- 16 GB of memory is comfortable: compiling a game takes several GB.
- About 8 GB of free disk space for a game.
- Your own copy of one of the games below, and a DVD drive to read it. An
  image you already made of your own disc works too.
- A game controller if you have one (Xbox, PlayStation, Switch Pro and most
  others work). The keyboard works too.
- An internet connection, to download the tools and to send your logs.

The games were measured on a 14-core desktop processor. Slower machines have
not been tried, and how yours copes is worth reporting.

### The games

| Game | Release | Serial | How far it gets |
|---|---|---|---|
| 007: Agent Under Fire | Europe (PAL) | SLES-50539 | the menus, the first mission and the second mission's race |
| Resident Evil Code: Veronica X | USA (NTSC) | SLUS-20184 | the menus, the movies and the first areas |

The serial is printed on the disc and on the back of the case. Another
release of these games (another region, or a reprint with another serial)
holds different programs and needs a set-up of its own. Any other game can
be tried too ([Another game](#another-game)), but expect it to stop early.
[docs/games.md](docs/games.md) has each game's known problems.

## 1. Install the tools

Open a terminal and run the commands for your distribution. `sudo` asks for
your password.

Ubuntu, Linux Mint, Pop!_OS, Debian:

```sh
sudo apt update
sudo apt install build-essential git cmake ninja-build clang libsdl2-dev libarchive-tools curl
```

Fedora:

```sh
sudo dnf install git cmake ninja-build clang gcc-c++ SDL2-devel bsdtar curl
```

Arch, Manjaro, EndeavourOS:

```sh
sudo pacman -S --needed base-devel git cmake ninja clang sdl2-compat libarchive curl
```

Git downloads PS2Native; CMake, Ninja and Clang build it; SDL2 gives the
games their window, sound and controllers; `bsdtar` takes files out of a disc
image; `curl` sends your logs.

Check that the compiler finds its files:

```sh
echo '#include <iostream>' | clang++ -std=c++20 -x c++ -fsyntax-only - && echo "The compiler works."
```

It should print `The compiler works.` If it says `'iostream' file not found`
instead, see [When something goes wrong](#when-something-goes-wrong).

## 2. Build PS2Native

Everything goes in a folder named `ps2` in your home folder. Download
PS2Native and prepare its build:

```sh
mkdir -p ~/ps2
cd ~/ps2
git clone https://github.com/luisbraganca/ps2n.git
cd ps2n
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
```

Near the end, the output should have a line like
`-- ps2n runtime: SDL2 2.30.0 found, window output enabled` (your version may
differ). If it says `SDL2 not found` instead, the games would have no window:
install SDL2 (step 1) and run the `cmake -S` command again.

Build it (a few minutes) and run its tests (under a minute):

```sh
cmake --build build
ctest --test-dir build -j 4 --output-on-failure 2>&1 | tee ~/ps2/tests.log
```

The last lines should say `100% tests passed`. If some tests fail, send
`~/ps2/tests.log` to the developer and carry on.

Last, run a tiny PS2 program that the build recompiled:

```sh
./build/examples/hello/hello --no-window
```

Among PS2Native's own messages (the lines that start with `[info:`), it
prints `Hello from a recompiled PS2 program!` and the results of a few
checks, ending with `done`.

## 3. Turn on log sending

This step is optional, but it saves you sending files: each session's log
goes to Axiom, a log service, where the developer reads it. You need a token
from the developer, a long code that starts with `xaat-`.

Put your token between the quotes of this first command, then run both
commands:

```sh
TOKEN='xaat-paste-your-token-here'
```

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/PS2Native"
printf '\n[Axiom]\nToken = %s\nDataset = ps2n-beta\nDomain = eu-central-1.aws.edge.axiom.co\n' "$TOKEN" >> "${XDG_CONFIG_HOME:-$HOME/.config}/PS2Native/ps2n.ini"
```

This adds an `[Axiom]` section to PS2Native's settings file,
`~/.config/PS2Native/ps2n.ini`. If the developer gave you another dataset or
domain, use those. When a game starts, the terminal now shows
`[info:axiom] sending this run's log to Axiom`.

What is sent:

- PS2Native's messages from the terminal: the `[info]`, `[warn]` and
  `[error]` lines;
- how fast the game ran, every ten seconds, and the summary at the end;
- your Linux version, processor, number of threads and display system (X11
  or Wayland);
- the game's command line, with your home folder written as `~`.

Nothing else: no screenshots, no files and nothing from other programs. To
stop sending, add the line `Enabled = false` under `[Axiom]` in the settings
file.

## 4. Set up your game

Each game gets a folder in `~/ps2`. In it go an image of the disc, the
game's programs taken out of the image, the game's config, which tells the
recompiler what to translate, and the build.

To make the image, put the disc in the drive and find the drive's name:

```sh
lsblk -o NAME,TYPE,SIZE,LABEL | grep rom
```

It is usually `sr0`, as in the commands below; if yours is `sr1`, write
`/dev/sr1` instead. Copying the disc takes 5 to 15 minutes. If `dd` says
`Permission denied`, run the same command with `sudo` in front, then make
the image yours with `sudo chown "$USER": cvx.iso` (or `auf.iso`, or
`game.iso`). If you already have an image of your own disc, copy it into the
folder under the name the commands use instead of running `dd`, for example
`cp ~/Downloads/my-disc.iso cvx.iso`.

### Resident Evil Code: Veronica X

Make the folder and the image, and check the release:

```sh
mkdir -p ~/ps2/code-veronica-x
cd ~/ps2/code-veronica-x
dd if=/dev/sr0 of=cvx.iso bs=2048 status=progress
bsdtar -xOf cvx.iso SYSTEM.CNF
```

The last command must show `BOOT2 = cdrom0:\SLUS_201.84;1`. Take the program
out of the image, and copy the game's config from PS2Native's `games` folder:

```sh
mkdir -p disc
bsdtar -xf cvx.iso -C disc SLUS_201.84
cp ~/ps2/ps2n/games/code-veronica-x/*.toml .
```

Recompile the game and build it:

```sh
~/ps2/ps2n/build/recompiler/ps2n recompile cvx.toml
cmake -S out/cvx -B out/cvx/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build out/cvx/build
```

The recompile takes seconds and lists warnings, which are normal. The build
compiles about 50 MB of C++, which takes from a few minutes to half an hour
or more. If it stops with `Killed`, or the computer runs out of memory, run
it again with fewer jobs at a time: `cmake --build out/cvx/build -j 2`.

Last, make a launcher, which starts the game from its folder and keeps each
session's log in `logs/`:

```sh
cat > play.sh <<'EOF'
#!/bin/sh
# Starts the game, keeping its log in logs/.
cd "$(dirname "$0")" || exit 1
mkdir -p logs
./out/cvx/build/cvx --cdrom cvx.iso "$@" 2>&1 | tee -i "logs/$(date +%Y-%m-%d_%H-%M-%S).log"
EOF
chmod +x play.sh
```

From now on, start the game with:

```sh
~/ps2/code-veronica-x/play.sh
```

### 007: Agent Under Fire

The game is four programs: a loader, a movie player, the game proper (one
program per language; `ACTION.UK` is the English one) and the driving
missions. Make the folder and the image, and check the release:

```sh
mkdir -p ~/ps2/agent-under-fire
cd ~/ps2/agent-under-fire
dd if=/dev/sr0 of=auf.iso bs=2048 status=progress
bsdtar -xOf auf.iso SYSTEM.CNF
```

The last command must show `BOOT2=cdrom0:\SLES_505.39;1`. Take the programs
out of the image, and copy the game's configs from PS2Native's `games`
folder:

```sh
mkdir -p disc
bsdtar -xf auf.iso -C disc SLES_505.39 MOVIE.ELF ACTION.UK DRIVING.ELF
cp ~/ps2/ps2n/games/agent-under-fire/*.toml .
```

Recompile the game and build it:

```sh
~/ps2/ps2n/build/recompiler/ps2n recompile auf.toml
cmake -S out/auf -B out/auf/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build out/auf/build
```

This build compiles about 110 MB of C++, twice as much as Code: Veronica X.
If it stops with `Killed`, run it again with `-j 2` at the end. Then make the
launcher; `--region europe` is there because this is a PAL game:

```sh
cat > play.sh <<'EOF'
#!/bin/sh
# Starts the game, keeping its log in logs/.
cd "$(dirname "$0")" || exit 1
mkdir -p logs
./out/auf/build/auf --cdrom auf.iso --region europe "$@" 2>&1 | tee -i "logs/$(date +%Y-%m-%d_%H-%M-%S).log"
EOF
chmod +x play.sh
```

From now on, start the game with:

```sh
~/ps2/agent-under-fire/play.sh
```

At the language screen, choose English. Only `ACTION.UK` is recompiled: the
other languages would run on PS2Native's interpreter, much too slowly to
play.

### Another game

Other games have not been tried, and most will stop early, but what happens
is worth reporting. Make the folder and the image, and look at the disc's
start-up file:

```sh
mkdir -p ~/ps2/other-game
cd ~/ps2/other-game
dd if=/dev/sr0 of=game.iso bs=2048 status=progress
bsdtar -xOf game.iso SYSTEM.CNF
```

The `BOOT2` line names the game's program, such as
`cdrom0:\SLUS_123.45;1`, and `VMODE` says `PAL` or `NTSC`. Put the program's
name, without `cdrom0:\` and `;1`, between the quotes of this command, and
run it:

```sh
BOOT='SLUS_123.45'
```

Then take the program out of the image, write a config, recompile, build,
and make the launcher. Copy the whole block:

```sh
mkdir -p disc
bsdtar -xf game.iso -C disc "$BOOT"
printf '[input]\nelf = "disc/%s"\n\n[output]\ndir = "out/game"\nname = "game"\n\n[analysis]\nentries_files = ["ps2n_missing.toml"]\n' "$BOOT" > game.toml
~/ps2/ps2n/build/recompiler/ps2n recompile game.toml
cmake -S out/game -B out/game/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build out/game/build
cat > play.sh <<'EOF'
#!/bin/sh
# Starts the game, keeping its log in logs/.
cd "$(dirname "$0")" || exit 1
mkdir -p logs
./out/game/build/game --cdrom game.iso "$@" 2>&1 | tee -i "logs/$(date +%Y-%m-%d_%H-%M-%S).log"
EOF
chmod +x play.sh
```

Start it with `~/ps2/other-game/play.sh`, or, when `VMODE` said `PAL`, with
`~/ps2/other-game/play.sh --region europe`. For a second game, use another
folder name in place of `other-game`.

Many games start further programs from the disc. Those run on the
interpreter, very slowly, until they are added to the config: the log then
says `has no recompiled code; interpreting it`, and the developer can tell
you what to add.

## 5. Play

Connect your controller before you start the game. It is set up by itself
only the first time, and only if there is no settings file yet. If you made
one in step 3, or the controller does nothing, press Escape and choose
Controller Settings, then Controller Port 1, then Automatic Mapping. The
settings file keeps it for every game.

The keyboard works as a controller too:

| Key | Controller | | Key | Controller |
|---|---|---|---|---|
| arrows | d-pad | | `W` `A` `S` `D` | left stick |
| `K` | Cross | | `T` `F` `G` `H` | right stick |
| `L` | Circle | | `Q`, `E` | L1, R1 |
| `J` | Square | | `1`, `3` | L2, R2 |
| `I` | Triangle | | `2`, `4` | L3, R3 |
| Return | Start | | Backspace | Select |

And these keys work while you play:

| Key | What it does |
|---|---|
| Escape | the pause menu: Resume Game, Controller Settings, Graphics Settings, Toggle Fullscreen, Quit Game |
| F11, or Alt+Return | fullscreen |
| F8 | a screenshot, saved in the game folder's `snaps/` |
| Space | pause |
| Tab | turbo: as fast as the computer can go, on and off |
| F5 | how the picture's two fields are joined: adaptive, weave or bob |
| F6 | the picture's shape: Stretch, Auto, 4:3, 16:9 or 10:7 |

To quit, close the window, choose Quit Game in the pause menu, or press
Ctrl+C in the terminal. The terminal then prints a list of what the game
asked for that PS2Native does not do yet. The game saves to memory cards,
which are folders in the game folder's `memcards/`.

While the game runs, the terminal fills with PS2Native's messages. Warnings
are normal: each is something the game asked for that PS2Native does not do
yet, and the developer reads them in the log.

A game's folder then holds:

```
cvx.iso                 the disc image
disc/                   the game's programs, taken out of the image
cvx.toml                the game's config, from PS2Native's games folder
out/cvx/                the recompiled game; out/cvx/build/cvx is the program
play.sh                 the launcher
logs/                   a log per session
memcards/               the memory cards
snaps/                  the screenshots
ps2n_missing*.toml      the code the game ran on the interpreter
```

## 6. Report what happened

When something looks, sounds or behaves wrong, note:

- the game, and where you were: a menu, a mission, a room;
- what you did, what you expected, and what happened instead;
- the date and time, so that the developer finds the session's log;
- a screenshot (F8), or a phone video for sound, flicker or movement.

Send it to the developer, or open an issue at
<https://github.com/luisbraganca/ps2n/issues>. If the game crashed or froze,
also send that session's log from the game folder's `logs/`: a crash loses
the last seconds of messages before Axiom gets them. If you did not turn on
log sending, send the log every time.

Worth reporting even when nothing breaks: how far you got, how smoothly the
game played, and how long the builds took on your computer.

## 7. Rebuild a game

Rebuild a game after updating PS2Native, and after a session that ran code
on the interpreter. Code the recompiler did not find by itself runs there,
which is slower. When the game exits, it writes that code's addresses to the
`ps2n_missing` files in its folder (the log says
`interpreted entry point(s) written to`), and a rebuild recompiles that code
for the next session. The `cp` line copies the game's configs again, in case
an update changed them.

When the developer asks you to update PS2Native:

```sh
cd ~/ps2/ps2n
git pull
cmake --build build
```

Then rebuild your game. Code: Veronica X:

```sh
cd ~/ps2/code-veronica-x
cp ~/ps2/ps2n/games/code-veronica-x/*.toml .
~/ps2/ps2n/build/recompiler/ps2n recompile cvx.toml
cmake --build out/cvx/build
```

Agent Under Fire:

```sh
cd ~/ps2/agent-under-fire
cp ~/ps2/ps2n/games/agent-under-fire/*.toml .
~/ps2/ps2n/build/recompiler/ps2n recompile auf.toml
cmake --build out/auf/build
```

Another game:

```sh
cd ~/ps2/other-game
~/ps2/ps2n/build/recompiler/ps2n recompile game.toml
cmake --build out/game/build
```

## When something goes wrong

- **A command fails.** Run it again with ` 2>&1 | tee error.log` added at
  the end, and send `error.log` to the developer, with the command.
- **`'iostream' file not found`**, or another C++ header. Clang uses the C++
  files of an installed GCC and picked a version whose files are missing.
  Find which:

  ```sh
  clang++ -v -x c++ -E - < /dev/null 2>&1 | grep 'Selected GCC'
  ```

  The path it shows ends with a number, such as
  `/usr/lib/gcc/x86_64-linux-gnu/14`. On Ubuntu, Linux Mint and Debian,
  install that version's files, putting the number it showed in place of 14:

  ```sh
  sudo apt install libstdc++-14-dev
  ```

  Then run the compiler check of step 1 again.
- **`SDL2 not found`** when a build is prepared. Install SDL2's package
  (step 1), then run the `cmake -S` command again.
- **The build stops with `Killed`**, or the computer freezes while it builds.
  It ran out of memory: run the build command again with `-j 2` at the end.
- **The game freezes.** Wait ten seconds, then press Ctrl+C in the terminal:
  the game stops and prints its summary. If nothing happens, open another
  terminal and run `pkill -KILL -x cvx` (or `auf`, or `game`), and send the
  session's log.
- **A controller does nothing.** Press Escape and choose Controller Settings,
  then Controller Port 1, then Automatic Mapping.
- **No window opens, or the game stops at once.** Send the session's log.

## On Windows

Windows 10 or 11, 64-bit. The steps are the same; these are the commands
that differ. Run them in PowerShell.

**1. The tools.** Git, Visual Studio's Build Tools (with Clang, CMake and
Ninja) and SDL2:

```powershell
winget install --id Git.Git -e
winget install --id Microsoft.VisualStudio.BuildTools -e --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.CMake.Project"
New-Item -ItemType Directory -Force C:\ps2\deps | Out-Null
Invoke-WebRequest https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-VC.zip -OutFile C:\ps2\deps\SDL2.zip
Expand-Archive C:\ps2\deps\SDL2.zip -DestinationPath C:\ps2\deps
```

Then close PowerShell and open a new one. The compiler only works in a
PowerShell window prepared for it: paste this first in every new window you
build in.

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath; Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"; Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
```

**2. Build PS2Native**, in `C:\ps2`:

```powershell
cd C:\ps2
git clone https://github.com/luisbraganca/ps2n.git
cd ps2n
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DSDL2_DIR=C:/ps2/deps/SDL2-2.32.10/cmake
cmake --build build
ctest --test-dir build -j 4 --output-on-failure
.\build\examples\hello\hello.exe --no-window
```

**3. Log sending.** The settings file is `%APPDATA%\PS2Native\ps2n.ini`:

```powershell
$token = 'xaat-paste-your-token-here'
New-Item -ItemType Directory -Force "$env:APPDATA\PS2Native" | Out-Null
Add-Content -Encoding ascii "$env:APPDATA\PS2Native\ps2n.ini" "`r`n[Axiom]`r`nToken = $token`r`nDataset = ps2n-beta`r`nDomain = eu-central-1.aws.edge.axiom.co"
```

**4. The game.** Windows has no command that copies a disc: make an ISO
image with a disc imaging program (ImgBurn's "Create image file from disc",
for example) and save it in the game's folder under the name the commands
use, such as `C:\ps2\code-veronica-x\cvx.iso`. Windows' own `tar` takes
files out of it, as `bsdtar` does on Linux:

```powershell
cd C:\ps2\code-veronica-x
tar -xOf cvx.iso SYSTEM.CNF
mkdir disc
tar -xf cvx.iso -C disc SLUS_201.84
```

Then copy the game's configs, recompile and build:

```powershell
Copy-Item C:\ps2\ps2n\games\code-veronica-x\*.toml .
C:\ps2\ps2n\build\recompiler\ps2n.exe recompile cvx.toml
cmake -S out/cvx -B out/cvx/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DSDL2_DIR=C:/ps2/deps/SDL2-2.32.10/cmake
cmake --build out/cvx/build
```

The launcher is a `play.cmd` that keeps the log in `logs\`; double-click it
to play. For Agent Under Fire, write `out\auf\build\auf.exe --cdrom auf.iso
--region europe` in its last line:

```powershell
@'
@echo off
rem Starts the game, keeping its log in logs\.
cd /d "%~dp0"
if not exist logs mkdir logs
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH-mm-ss"') do set T=%%t
out\cvx\build\cvx.exe --cdrom cvx.iso %* 2> "logs\%T%.log"
'@ | Set-Content -Encoding ascii play.cmd
```

**When it freezes**, run `taskkill /F /IM cvx.exe` (or `auf.exe`) in
another window.
