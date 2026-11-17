// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compiled code in a module of its own (docs/phase4-proposal.md, steps P4.1
// and P4.2).
//
// The compiled code of a game is built into the host, or, with
// PS2N_GAME_MODULE (tests/tests.cmake), into modules of their own, DLLs that
// the host loads when it starts: what a user's computer is to build (ADR
// 0028). In a module, compiled code reaches the machine's state and the
// runtime across the module's boundary, through what the host exports:
//
//  - the runtime's variables and functions that compiled code uses are
//    declared PS2N_API: exported by the host, imported by a module;
//  - PCSX2's variables and functions that it uses are exported by the host
//    under their own symbols (tests/tests.cmake), which a module imports
//    (ps2n/pcsx2.h).
//
// A module is compiled with PS2N_MODULE, against PS2Native's own headers
// and nothing else: no header of PCSX2's, no standard library
// (ps2n/pcsx2_layout.h). Built into the host, as compiled code has been,
// this changes nothing. With PS2N_GAME_IMPORTS the code is compiled as for a
// module and linked into the host all the same, whose linker then makes the
// import table's entries itself: what reaching the core through the import
// table costs, alone (ADR 0029).
#pragma once

#if defined(_WIN32) && defined(PS2N_MODULE)
#define PS2N_API __declspec(dllimport)
#elif defined(_WIN32) && defined(PS2N_EXPORTS)
#define PS2N_API __declspec(dllexport)
#else
#define PS2N_API
#endif
