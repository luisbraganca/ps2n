# PS2Native's changes to PCSX2's code

PS2Native is built on PCSX2's core, which is in this repository as PCSX2
released it: the submodule `third_party/pcsx2`, pinned to a release of
PCSX2's and never edited. What PS2Native changes of PCSX2's code is here, one
patch to a change, applied in the order of their names to a copy when
PS2Native's runtime is built (`cmake/ps2n-pcsx2-patches.cmake`). Each patch
says at its top what it changes, why, and when it was made, and is under
PCSX2's licence, the GNU General Public License, version 3 or later.

| Change | Since | What it does |
|---|---|---|
| `0001-the-vu-thread-is-not-woken-where-it-is-not-in-use` | 2026-11-22 | VIF1's STROW and STCOL commands, and writes of its row and column registers, wake PCSX2's VU thread only while that thread is in use. Where it is not (every run but a game played in a window with the setting on, since ADR 0051), a game that sends many of them paid for the wakes all the same. With it the setting must not be switched on while a game runs: PS2Native decides it once, when the game's disc is read. |
| `0002-fullscreenui-settings-includes-sstream` | 2026-11-27 | `pcsx2/ImGui/FullscreenUI_Settings.cpp` includes `<sstream>`, which it uses and, since PCSX2 v2.9.196, no longer gets through `common/StringUtil.h`. Without it PCSX2's source does not compile on Linux here (Ubuntu 22.04's libstdc++). It changes nothing a run does, and so stock PCSX2's tree is built with it too ([docs/adr/0062](../../docs/adr/0062-ps2native-moves-to-pcsx2-2-9-198-carrying-a-missing-include.md)). It goes when PCSX2's own code includes it. |

- **They are PS2Native's, not PCSX2's.** A run that is compared with stock
  PCSX2 for time is compared with PCSX2 built without them
  (`build.ps1 -Pcsx2Patches off`, `build.sh --pcsx2-patches off`), but for a
  change that only lets PCSX2's source compile here and changes nothing a run
  does. Such a change says so on a line of its own in its header, "Stock
  PCSX2's tree is built with it too.", which `cmake/ps2n-pcsx2-patches.cmake`
  reads: without it that tree would not build.
- **Each is decided by the maintainer** before it is here
  ([docs/adr/0050](../../docs/adr/0050-pcsx2s-vu-thread-not-woken-where-it-is-not-in-use-ps2natives-first-change-to-pcsx2s-code.md)),
  and goes when the PCSX2 pinned here no longer needs it.
- **What can be carried this way** is a change to a file PCSX2's build
  compiles (`.cpp`, `.c`). A change to a header needs PCSX2's source copied
  whole, which the build does not do: `cmake/ps2n-pcsx2-patches.cmake` says
  why, and stops at such a patch.
- **A release says so.** The folder of a release has these patches in
  `licences/PCSX2-changes`, and its `licences/README.txt` says that PCSX2's
  core in it is changed.
