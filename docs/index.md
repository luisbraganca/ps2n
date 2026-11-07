---
layout: home
title: PlayStation 2, recompiled.
description: PS2Native translates PlayStation 2 Emotion Engine executables into readable C++ and links them with a native runtime.
image: /assets/brand/ps2native-social-1280x640.png
---
<main id="main-content">
  <section class="hero">
    <div class="hero-grid" aria-hidden="true"></div>
    <div class="hero-glow" aria-hidden="true"></div>
    <div class="pixel pixel-one" aria-hidden="true"></div>
    <div class="pixel pixel-two" aria-hidden="true"></div>
    <div class="pixel pixel-three" aria-hidden="true"></div>
    <div class="shell hero-layout">
      <div class="hero-copy">
        <div class="eyebrow entrance entrance-one"><span class="status-dot"></span> PlayStation 2 / Static recompilation</div>
        <img class="hero-logo entrance entrance-two" src="{{ '/assets/brand/ps2native-horizontal-dark.png' | relative_url }}" alt="PS2Native" width="340" height="106">
        <h1 class="entrance entrance-three">Old silicon.<br><span>New instructions.</span></h1>
        <p class="hero-intro entrance entrance-four">Turn PlayStation 2 Emotion Engine executables into readable, buildable C++. PS2Native pairs the translated code with a purpose-built runtime, giving original instructions a new home on modern hardware.</p>
        <div class="hero-actions entrance entrance-five">
          <a class="button button-primary" href="https://github.com/luisbraganca/ps2n/blob/main/GETTING_STARTED.md">Get started <span aria-hidden="true">↗</span></a>
          <a class="button button-quiet" href="#how-it-works">Explore the pipeline <span aria-hidden="true">↓</span></a>
        </div>
        <p class="development-note entrance entrance-six"><span>WORK IN PROGRESS</span> Two games in-game. One at menus.<br>Not yet playable from start to finish.</p>
      </div>

      <div class="translation-window entrance entrance-four" data-tilt>
        <div class="window-chrome">
          <span class="window-title"><span class="terminal-prompt" aria-hidden="true">›_</span> translation_session</span>
          <span class="window-label">PS2N / 001</span>
        </div>
        <figure class="translation-figure">
          <div class="translation-scene" aria-hidden="true">
            <div class="scene-grid"></div>
            <img class="scene-emblem" src="{{ '/assets/brand/ps2native-icon-blue-512.png' | relative_url }}" alt="" width="240" height="240">
            <span class="scene-coordinate coordinate-top">0x00100178</span>
            <span class="scene-coordinate coordinate-bottom">R5900 → NATIVE</span>
            <div class="pipeline">
              <div class="pipeline-stage stage-one">
                <span class="stage-number">01</span>
                <span class="stage-icon">&lt;/&gt;</span>
                <span class="stage-copy"><strong>Discover</strong><small>game.elf</small></span>
                <span class="stage-state">READ</span>
              </div>
              <div class="pipeline-connector connector-one"><span></span></div>
              <div class="pipeline-stage stage-two">
                <span class="stage-number">02</span>
                <span class="stage-icon">↦</span>
                <span class="stage-copy"><strong>Translate</strong><small>readable C++</small></span>
                <span class="stage-state">BUILD</span>
              </div>
              <div class="pipeline-connector connector-two"><span></span></div>
              <div class="pipeline-stage stage-three">
                <span class="stage-number">03</span>
                <span class="stage-icon">▷</span>
                <span class="stage-copy"><strong>Execute</strong><small>native program</small></span>
                <span class="stage-state">RUN</span>
              </div>
            </div>
          </div>
          <figcaption class="sr-only">PS2Native discovers functions in a PlayStation 2 ELF, translates instructions to readable C++, and links them with its runtime to create a native executable.</figcaption>
        </figure>
        <div class="translation-footer"><span><i></i> ONE INSTRUCTION. ONE SHARED IMPLEMENTATION.</span><span>→</span></div>
      </div>
    </div>

    <div class="shell metric-strip entrance entrance-six">
      <div class="metric"><strong data-count="359">359</strong><span>/ 359</span><small>EE instructions covered</small></div>
      <div class="metric"><strong>1–8<span>×</span></strong><small>Vulkan internal resolution</small></div>
      <div class="metric"><strong data-count="3">3</strong><span>retail games</span><small>Being brought up, one trace at a time</small></div>
      <div class="metric metric-message"><span class="mini-brackets">[ ]</span><p>Original game code.<br>No bundled BIOS or game data.</p></div>
    </div>
  </section>

  <section class="section shell" id="how-it-works">
    <div class="section-heading reveal">
      <div>
        <p class="eyebrow">01 / A different approach</p>
        <h2>A bridge between<br>two architectures.</h2>
      </div>
      <p class="section-intro">Not a black box. A readable translation of the original program, joined to a runtime that supplies the rest of the console.</p>
    </div>

    <div class="feature-grid">
      <article class="feature-card reveal" style="--delay: 0ms">
        <div class="feature-top"><span class="feature-number">01</span><span class="feature-symbol" aria-hidden="true">⌗</span></div>
        <h3>Readable by design.</h3>
        <p>Guest functions become ordinary C++ functions. Branches become labels and gotos; direct calls stay direct. The original instruction sits beside its translation.</p>
        <span class="feature-caption">SOURCE YOU CAN FOLLOW</span>
      </article>
      <article class="feature-card reveal" style="--delay: 100ms">
        <div class="feature-top"><span class="feature-number">02</span><span class="feature-symbol" aria-hidden="true">≡</span></div>
        <h3>One source of truth.</h3>
        <p>Recompiled and interpreted code share the same instruction semantics. A fix in one place applies to both paths, making behavioural differences easier to isolate.</p>
        <span class="feature-caption">SHARED INSTRUCTION SEMANTICS</span>
      </article>
      <article class="feature-card reveal" style="--delay: 200ms">
        <div class="feature-top"><span class="feature-number">03</span><span class="feature-symbol" aria-hidden="true">↗</span></div>
        <h3>Keep moving forward.</h3>
        <p>An interpreter runs code that analysis missed and records entry points for the next recompile. Missing coverage costs speed instead of immediately ending the journey.</p>
        <span class="feature-caption">INTERPRETER FALLBACK</span>
      </article>
    </div>

    <div class="code-story reveal">
      <div class="code-story-copy">
        <p class="eyebrow">From guest to host</p>
        <h3>The original idea.<br>A native expression.</h3>
        <p>The recompiler preserves the program’s structure while a shared runtime handles the Emotion Engine kernel, vector units, GS, IOP, SPU2 and more.</p>
        <ul class="technical-list">
          <li><span>01</span> Direct C++ calls and resume points</li>
          <li><span>02</span> Deterministic software rendering</li>
          <li><span>03</span> Higher-resolution Vulkan graphics</li>
        </ul>
        <a class="text-link" href="architecture.md">Explore the architecture <span aria-hidden="true">↗</span></a>
      </div>
      <div class="code-window">
        <div class="code-window-header"><span><i></i><i></i><i></i></span><span>recomp_funcs_000.cpp</span><span class="code-language">C++</span></div>
        <pre class="code-sample"><code><span class="code-comment">// 0x00100178 · fib</span>
<span class="code-type">void</span> <span class="code-function">fib_00100178</span>() {
    <span class="code-function">ADDIU</span>(SP, SP, <span class="code-number">-32</span>);
    <span class="code-function">SD</span>(RA, SP, <span class="code-number">0</span>);
    <span class="code-function">SD</span>(A0, SP, <span class="code-number">16</span>);

    <span class="code-function">setLink</span>(RA, <span class="code-number">0x001001a4u</span>);
    <span class="code-function">ADDIU</span>(A0, A0, <span class="code-number">-1</span>);
    <span class="code-function">PS2_CALL</span>(<span class="code-number">0x00100178u</span>,
             fib_00100178,
             <span class="code-number">0x001001a4u</span>);

    <span class="code-function">ADDU</span>(S0, V0, ZERO);
}</code></pre>
        <div class="code-window-footer"><span class="status-dot"></span> ORIGINAL INSTRUCTIONS. NATIVE POSSIBILITIES.<span>ABOUT 10× IN A MEASURED INTEGER LOOP</span></div>
      </div>
    </div>
  </section>

  <section class="compatibility-section" id="compatibility">
    <div class="shell section">
      <div class="section-heading reveal">
        <div>
          <p class="eyebrow">02 / Compatibility</p>
          <h2>A work in progress.<br>Progress you can see.</h2>
        </div>
        <p class="section-intro">Three retail releases are being brought up. Gameplay starts in two; the third reaches its main menu. None is playable end to end yet.</p>
      </div>
      <div class="game-list reveal">
        <a class="game-row" href="games.md#007-agent-under-fire">
          <span class="game-index">01</span>
          <span class="game-name"><strong>007: Agent Under Fire</strong><small>Europe · SLES-50539</small></span>
          <span class="game-scope">First mission complete; driving race reached</span>
          <span class="status-badge ingame">IN-GAME</span><span class="row-arrow" aria-hidden="true">↗</span>
        </a>
        <a class="game-row" href="games.md#resident-evil-code-veronica-x">
          <span class="game-index">02</span>
          <span class="game-name"><strong>Resident Evil Code: Veronica X</strong><small>USA · SLUS-20184</small></span>
          <span class="game-scope">Menus, movies, first areas and saving</span>
          <span class="status-badge ingame">IN-GAME</span><span class="row-arrow" aria-hidden="true">↗</span>
        </a>
        <a class="game-row" href="games.md#secret-agent-clank">
          <span class="game-index">03</span>
          <span class="game-name"><strong>Secret Agent Clank</strong><small>USA · SCUS-97623</small></span>
          <span class="game-scope">Start screen and main menu; no level loaded</span>
          <span class="status-badge menus">MENUS</span><span class="row-arrow" aria-hidden="true">↗</span>
        </a>
      </div>
      <div class="compatibility-footer reveal"><p><span class="status-dot amber"></span> In-game means gameplay starts with major gaps—not that the whole game works.</p><a class="text-link" href="games.md">Full compatibility notes <span aria-hidden="true">↗</span></a></div>
    </div>
  </section>

  <section class="section shell" id="documentation">
    <div class="section-heading reveal">
      <div>
        <p class="eyebrow">03 / Documentation</p>
        <h2>Look under<br>the hood.</h2>
      </div>
      <p class="section-intro">From your first build to the renderer’s memory model. The same technical documentation, now easier to explore.</p>
    </div>
    <div class="docs-grid">
      <a class="doc-card reveal" style="--delay: 0ms" href="https://github.com/luisbraganca/ps2n/blob/main/GETTING_STARTED.md"><span class="doc-card-number">01 / SETUP</span><h3>Getting started</h3><p>Build PS2Native, prepare a disc and run your first game.</p><span class="doc-card-link">Start here ↗</span></a>
      <a class="doc-card reveal" style="--delay: 60ms" href="architecture.md"><span class="doc-card-number">02 / DESIGN</span><h3>Architecture</h3><p>Execution, scheduling, memory and the rendering model.</p><span class="doc-card-link">Read the design ↗</span></a>
      <a class="doc-card reveal" style="--delay: 120ms" href="runtime.md"><span class="doc-card-number">03 / REFERENCE</span><h3>Runtime</h3><p>Options, rendering, coverage and debugging tools.</p><span class="doc-card-link">Explore the runtime ↗</span></a>
      <a class="doc-card reveal" style="--delay: 0ms" href="config.md"><span class="doc-card-number">04 / CONFIGURATION</span><h3>Config reference</h3><p>Describe a game and shape its native translation.</p><span class="doc-card-link">Configure a game ↗</span></a>
      <a class="doc-card reveal" style="--delay: 60ms" href="games.md"><span class="doc-card-number">05 / TESTING</span><h3>Game notes</h3><p>Tested scope, known issues and measured performance.</p><span class="doc-card-link">Check compatibility ↗</span></a>
      <a class="doc-card reveal" style="--delay: 120ms" href="input.md"><span class="doc-card-number">06 / INPUT</span><h3>Controllers</h3><p>DualShock 2 bindings, hotkeys and graphics settings.</p><span class="doc-card-link">Take control ↗</span></a>
    </div>
  </section>

  <section class="shell launch-section reveal">
    <div class="launch-copy">
      <p class="eyebrow">04 / Start exploring</p>
      <h2>Give the original<br>a new address.</h2>
      <p>Bring your own game files. Follow the instructions.<br>See what the original code can become.</p>
      <a class="text-link" href="https://github.com/luisbraganca/ps2n">View the repository <span aria-hidden="true">↗</span></a>
    </div>
    <div class="quickstart">
      <div class="quickstart-header"><span>YOUR FIRST BUILD</span><button type="button" data-copy-target="quickstart-command" aria-label="Copy build commands">Copy</button></div>
      <pre id="quickstart-command"><code>git clone https://github.com/luisbraganca/ps2n.git
cd ps2n
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build</code></pre>
      <p>Windows x64 tested · CMake 3.20+ · C++20 · Ninja</p>
    </div>
  </section>
</main>
