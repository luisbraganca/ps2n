<!-- The project brief as given by the maintainer on 2026-11-10, verbatim. It is the authoritative statement of goals and rules. Do not edit it to match an implementation; raise the conflict instead. -->

# PS2Native (ps2n) - Project Reboot

## Background

I previously developed an experimental PS2 static recompiler (see `../ps2-uncompiler`).

The project eventually became an endless cycle of AI-assisted debugging, hardware emulation problems, game-specific fixes, and enormous token consumption.

After investing significant effort into recompiling Killzone, I compared it against PCSX2. PCSX2 ran faster, looked significantly better, and offered much greater compatibility.

I want to reboot PS2Native with a fundamentally different philosophy:

**Stop reinventing what PCSX2 has already solved, while preserving the goal of genuine native recompilation.**

## 1. Core Vision

Build PS2Native (`ps2n`), an open-source tool that converts PlayStation 2 games into native applications for modern operating systems.

### Non-negotiable requirements

- Perform genuine ahead-of-time static recompilation of PS2 executable code into host-native code.
- Minimize hardware emulation wherever technically and practically feasible.
- Reuse PCSX2's proven implementations, algorithms, compatibility fixes, and infrastructure whenever beneficial.
- Prioritize maximum game compatibility and visual/audio quality.
- Produce standalone, launchable native applications.
- Avoid requiring an emulator installation on the end user's computer.
- Do not simply bundle a game with a customized PCSX2 emulator.

The intended result is a **genuinely native game with the smallest practical amount of PS2 hardware emulation or compatibility infrastructure**.

Native recompilation applies to executable code. It does not require converting original game assets into different formats.

Some hardware compatibility services may still be necessary. Their existence is acceptable if they are justified and designed around the native game rather than conventional full-system emulation.

Explore whether EE, IOP, VU, and dynamically loaded code can be translated ahead of time. Clearly identify which components are statically compiled, natively implemented, emulated, or unsupported.

Never silently substitute ordinary emulation, interpretation, or runtime JIT recompilation for the promised static recompilation. Any proposed fallback must be documented and approved.

## 2. Priorities

In descending order:

1. **Maximum compatibility:** Support as many PS2 games as realistically possible.
2. **Visual fidelity:** Achieve graphics quality comparable to PCSX2, including resolution scaling and applicable enhancements.
3. **Audio fidelity:** Preserve accurate sound behavior and synchronization.
4. **Maintainability:** Minimize custom hardware implementations, upstream divergence, and recurring debugging.
5. **Performance:** Aim to match or exceed PCSX2, but never sacrifice compatibility or visual quality solely for performance improvements.

PCSX2 is our reference implementation and baseline, not an arbitrary performance target we must beat.

## 3. PCSX2 Integration

Research the latest official PCSX2 source code and identify components we can reuse.

Investigate:

- Graphics synthesis, rendering backends, shaders, and internal resolution scaling.
- SPU2 and audio infrastructure.
- Input devices and controller handling.
- Memory cards and storage.
- Timing, synchronization, DMA, and hardware interfaces.
- Game compatibility database and automatic fixes.
- Widescreen patches, FPS patches, cheats, and enhancements.
- Game identification and region handling.
- Disc image support and filesystem access.
- Configuration management and defaults.
- Other reusable runtime components.

Prefer PCSX2-derived functionality to reinventing complicated PS2 hardware behavior.

However, avoid pulling entire emulator subsystems into the resulting application when a smaller native implementation would be practical.

Investigate dependency boundaries carefully.

### Upstream synchronization

- Track PCSX2's Nightly development channel.
- Pin specific upstream Git revisions for reproducibility.
- Design an automated or semi-automated synchronization process.
- Detect breaking API changes and compatibility regressions.
- Avoid maintaining a heavily modified fork unless absolutely necessary.
- Never blindly merge upstream changes.

## 4. Compilation Wizard

The end-user application should offer a straightforward wizard.

### Step 1 - Game selection

Allow users to select game files in formats supported by PCSX2, where technically feasible.

Identify the game, region, serial number, and available compatibility information.

### Step 2 - BIOS

Determine whether the selected architecture or game requires a PS2 BIOS.

If necessary, request a user-provided BIOS.

If unnecessary, do not require one.

Never distribute proprietary BIOS files.

### Step 3 - Configuration

Present applicable PCSX2 configuration options.

Examples include:

- Graphics renderer.
- Internal rendering resolution.
- Widescreen patches.
- FPS patches and frame-rate targets.
- Controller mappings.
- Audio settings.
- Compatibility settings.
- Relevant game patches and fixes.

Use PCSX2 defaults wherever possible.

The user must be able to accept every default without manually configuring anything.

### Step 4 - Recompilation

Analyze the game and perform native recompilation.

Show meaningful progress and diagnostics.

Clearly report unsupported features, compilation failures, and known limitations.

### Step 5 - Packaging

Generate a native application for the selected platform.

The resulting game may require access to the original disc image at runtime.

This is completely acceptable.

Game assets do not necessarily need to be embedded into the executable.

Do not bundle copyrighted commercial game data without appropriate authorization.

### Runtime configuration policy

**All game configuration is finalized during the compilation process.**

The resulting game must not expose PCSX2's settings interface.

Graphics, audio, controller mappings, enhancements, and compatibility options should be fixed according to the chosen build configuration.

The player should launch the native game without needing to understand PCSX2 or configure an emulator.

Allow internal runtime configuration only where technically unavoidable, without turning it into a general emulator settings UI.

Recompiling the game is the expected method for changing build-time settings.

## 5. Target Platforms

Initially support:

- Windows x64.
- Linux x64.
- macOS ARM64.
- macOS x64.

Consider Android and iOS for future development.

Design code generation and runtime abstractions to accommodate different CPU architectures.

Do not assume x86-64-specific techniques will work unchanged on ARM64.

Document platform-specific restrictions and limitations.

## 6. Licensing

PS2Native will be entirely open-source.

Adopt GPL-3.0-or-later as the initial licensing direction, subject to dependency compatibility verification.

Audit the licensing implications of reusing PCSX2 and its third-party dependencies.

Preserve copyright notices and attribution.

Document the licensing obligations of both PS2Native and generated native applications, including any linked or distributed PCSX2-derived code.

Do not distribute copyrighted game files or proprietary BIOS images.

## 7. Existing Project

Thoroughly inspect `../ps2-uncompiler` before implementing anything substantial.

Identify reusable:

- Static recompilation and executable analysis.
- Code-generation infrastructure.
- Hardware implementations.
- Runtime components.
- Tests and benchmarks.
- Compatibility fixes.
- Build systems and scripts.
- Documentation.
- Branding, icons, and logos.

You may rebuild the project from scratch if justified.

However, do not discard existing working functionality without evaluating it.

Preserve the name **PS2Native**, abbreviation **ps2n**, and existing branding.

The previous website is no longer required.

I will recreate the GitHub repository `luisbraganca/ps2n` myself.

Do not delete repositories or push to GitHub without explicit authorization.

## 8. Game Compatibility

This is a solo-maintained project.

Use four compatibility classifications:

- **Buildable:** Native compilation and linking complete successfully.
- **Bootable:** The game starts and reaches a menu or initial gameplay.
- **Playable:** Gameplay is functional, although some issues may remain.
- **Tested:** I have personally played a couple of levels at 1920x1080, with widescreen enabled and a target of 50/60 FPS where technically compatible.

"Tested" does not imply the entire game is bug-free.

Some games are tied to their original frame rates. Do not apply unsafe FPS modifications that break physics, animation, gameplay timing, or synchronization.

Distinguish actual game simulation speed from rendered FPS.

Prefer general compatibility improvements over isolated game-specific hacks.

## 9. Performance and Quality

Benchmark generated games against PCSX2 using equivalent configurations.

Evaluate:

- Frame rates and frame times.
- CPU/GPU utilization.
- Visual correctness.
- Audio correctness.
- Gameplay compatibility.
- Timing and synchronization.
- Runtime overhead.

Do not assume native recompilation automatically produces better performance.

Matching PCSX2's performance while maintaining its compatibility and visual quality is an acceptable outcome.

Never claim performance or compatibility improvements without reproducible verification.

## 10. Development Strategy

**Prove the architecture before building the application.**

### Phase 1 - Feasibility Assessment

Before implementing substantial code:

1. Audit the existing project.
2. Study PCSX2's current source code and architecture.
3. Investigate which PCSX2 components can realistically be reused.
4. Determine how statically recompiled code can communicate with those components.
5. Examine EE, IOP, VU, dynamically loaded code, memory mapping, DMA, interrupts, timing, and synchronization.
6. Identify hardware emulation that cannot realistically be eliminated.
7. Compare architectural alternatives and their maintenance costs.
8. Assess licensing and platform constraints.
9. Recommend a practical architecture.
10. Define a minimal proof of concept.

Do not assert feasibility based on theoretical assumptions alone.

If genuine static recompilation is incompatible with a proposed PCSX2 integration strategy, explain why and investigate alternatives.

**Stop after presenting the feasibility assessment and ask for my approval.**

### Phase 2 - Proof of Concept

Use a small legally usable PS2 homebrew program or controlled test executable.

Demonstrate:

- Genuine ahead-of-time native code generation.
- Correct execution of the translated code.
- Communication with necessary hardware/runtime components.
- Correct memory and timing behavior.
- Comparable results against PCSX2.

Prioritize validating the hardest technical problems.

Do not build the wizard, GUI, packaging system, or unrelated infrastructure yet.

### Phase 3 - First Real Game

Once the proof of concept succeeds:

- Select one user-provided PS2 game.
- Attempt genuine native recompilation.
- Implement missing functionality through reusable solutions.
- Compare results with PCSX2.
- Document compatibility, visual issues, and performance.
- Record limitations and regressions.

Avoid game-specific hardcoding unless unavoidable.

### Phase 4 - User Application

After validating the architecture:

- Implement the compilation wizard.
- Integrate game recognition and settings.
- Build the compilation pipeline.
- Package native applications.
- Provide meaningful diagnostics.
- Expand platform support.

### Phase 5 - Maintenance

- Implement automated regression tests.
- Maintain compatibility reports.
- Automate upstream synchronization checks.
- Track performance regressions.
- Document architecture and development workflows.
- Reduce manual maintenance requirements.

## 11. AI Development Rules

The previous project consumed excessive AI tokens through endless debugging loops.

Follow these principles:

- Investigate root causes before changing code.
- Prefer small, testable modifications.
- Write reproducible tests when practical.
- Avoid speculative trial-and-error debugging.
- After repeated failed attempts, stop and reassess assumptions.
- Keep concise technical documentation for future AI sessions.
- Maintain architecture decision records.
- Never invent successful tests or benchmark results.
- Never silently skip failing tests.
- Avoid unnecessary dependencies and custom infrastructure.
- Prefer proven solutions and simple abstractions.
- Avoid premature GUI or infrastructure development.
- Explicitly identify blockers and technical uncertainty.
- Challenge my assumptions when necessary.
- Favor long-term maintainability over quick fixes.

## 12. First Assignment

**Do not implement the complete application yet.**

Your first task is to:

1. Inspect `../ps2-uncompiler` thoroughly.
2. Research the current PCSX2 architecture, source code, and licensing.
3. Assess the technical feasibility of genuine static recompilation with minimal PS2 hardware emulation.
4. Identify which PCSX2 components can be reused without effectively embedding an entire emulator.
5. Recommend the most suitable architecture.
6. Explain which existing components should be retained.
7. Document major technical risks and tradeoffs.
8. Propose the smallest meaningful proof of concept.
9. Ask any remaining questions necessary to make an informed decision.

Be skeptical and evidence-driven.

Do not silently change the fundamental goals to make implementation easier.

**Stop after the feasibility assessment and architectural recommendation. Wait for my approval before starting implementation.**

Reference Projects

Study the following open-source projects as part of the initial architectural research:

AnyPS5: https://github.com/boykopovar/AnyPS5

PS2Recomp: https://github.com/ran-j/ps2recomp

PS2 Web Recomp: https://github.com/snuri00/ps2-web-recomp

For each project:

Inspect its source code, architecture, documentation, and implementation status.

Identify its approach to recompilation, runtime integration, hardware emulation, and compatibility.

Evaluate which components, algorithms, and architectural decisions could benefit PS2Native.

Investigate potential performance, compatibility, and portability tradeoffs.

Verify licensing compatibility before proposing code reuse.

Identify limitations or architectural mistakes we should avoid.

Compare these projects against PCSX2 and our previous ../ps2-uncompiler implementation.

Do not assume these projects are feature-complete, production-ready, or technically superior.

Use them as sources of inspiration, reusable knowledge, and potentially reusable code, but evaluate everything critically.

Include your findings in the Phase 1 feasibility assessment, with concrete source references and recommendations.

Our objective is not to reinvent existing solutions when a technically sound implementation already exists.
