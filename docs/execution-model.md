# Execution model

How statically compiled EE code, the fallback interpreter and the PCSX2 core
run a program together. The definitions are in code:
[runtime/include/ps2n/recomp.h](../runtime/include/ps2n/recomp.h) (the
contract generated code is written against),
[runtime/ee_provider.cpp](../runtime/ee_provider.cpp) (dispatcher,
interpreter, verification) and
[recompiler/src/codegen.cpp](../recompiler/src/codegen.cpp) (what is
generated). This file is the explanation; if it and the code disagree, the
code is right and this file needs fixing.

## The parts

- **The core** is PCSX2, unmodified: memory, the scheduler, every device, the
  BIOS. It asks its EE provider to `Execute()` and expects it to keep
  `cpuRegs.cycle` moving and to call `_cpuEventTest_Shared()` when
  `cpuRegs.cycle` reaches `cpuRegs.nextEventCycle`.
- **The provider** (`ps2n::ee::Provider()`) is PS2Native's `R5900cpu`. Its
  `Execute()` is a dispatcher loop: look up the PC; run the compiled function
  that has an entry there, or interpret one block.
- **Compiled code** is one C++ function per guest function, generated ahead of
  time by `ps2n-recomp`, in an *image* per executable. The image whose CRC
  (PCSX2's, the exclusive-or of the ELF's words) equals that of the executable
  the machine boots is used.
- **The interpreter** runs everything else: the BIOS, code that is not in the
  image, code that changed in memory. It is also the reference: a run with
  compiled code must be the same run as without (`ps2n-host -static off`).

There is no guest state outside the core. Registers are `cpuRegs` and
`fpuRegs`, memory is the core's, reached through its address table (vtlb). A
compiled function's host frame holds nothing that matters, which is what makes
every unwinding below safe.

## Blocks and time

Time is counted in blocks, by the interpreter and by compiled code alike:

- A block ends after a branch or jump **and its delay slot**, taken or not. A
  likely branch that is not taken has no delay slot.
- A block ends after `SYSCALL`, `BREAK`, `ERET`, `EI` and the trap
  instructions (`ps2n::r5900::isSystem`). After `ERET`, `EI` and the traps an
  event test is forced, as PCSX2's recompiler does.
- A block begins wherever the block before it ended. That is decided by
  execution, not by the recompiler: falling into a label does not end a block.
- A block costs the sum of its instructions' counts (`ps2n::r5900::cycles`,
  PCSX2's opcode table, in eighths of a cycle), divided by eight and rounded
  down, at least one: what PCSX2's recompiler adds for a block. The count is
  added to `cpuRegs.cycle` at the end of the block; within a block the cycle
  count does not move.
- At the end of a block, if `cpuRegs.cycle` has reached
  `cpuRegs.nextEventCycle`, the core's event test runs, with `cpuRegs.pc` set
  to the next instruction. Interrupts are taken there and only there.

Generated functions keep the running count in a local, `acc`, so that a block
entered in its middle (through a label) and one fallen into count the same as
in the interpreter. `PS2N_BLOCK_END(acc, eighths, next)` ends a block.

**This rule is the contract.** Change it in one place and the lockstep tests
fail. PCSX2's interpreter tests for events *before* the delay slot of a branch
that is not taken; PS2Native follows the recompiler's shape instead (the delay
slot belongs to the block).

Compiled code assumes COP0 `Config` bit 18 is set (two instructions issued per
cycle, which halves the counts), as the BIOS sets it. If it is clear, nothing
compiled runs. A compiled write to `Config` hands the rest of its block to the
interpreter.

**Idle loops.** When a block's branch goes back to the block's own first
instruction, and the block holds nothing but `nop`s, loads, and arithmetic on
constants and loaded values (`ps2n::r5900::WaitLoopScan`, PCSX2's own test),
the cycle count moves on to the next event instead of the loop running until
then. The interpreter decides it as it goes; the recompiler finds such loops
ahead of time and emits `PS2N_BLOCK_END_LOOP`, which skips only when the
block really began at the loop's start (it compares what the block cost with
what the loop costs). PCSX2's `WaitLoop` setting switches both off. This is
what keeps the BIOS's idle thread, which is interpreted, from eating host
time.

What PCSX2's recompiler does about time that is not reproduced:

- its EE cycle rate setting (counted as 0, with a warning);
- three more places where it ends a block (a 4 KB page boundary; a block it
  compiled earlier; before the target of a conditional branch back into the
  block), each of which costs it up to one cycle against this count. The
  second depends on what was executed first;
- its adding of the cycles so far to the cycle count in the middle of a
  block, which rounds down once more: before a COP2 instruction that may need
  VU0 to catch up, and before the COP0 instructions that read or write the
  count register or the performance counters or write the status register.
  Here the functions that carry those instructions out see the cycle count
  as it was when the block began;
- its skipping of count-down delay loops ("timeout loops") and of the
  `FlushCache` system calls, which it replaces by a fixed number of cycles;
  here the loop runs and the BIOS serves the call;
- the forced event test after a load from an EE timer register;
- `GoemonTlbHack`, a fix for one game.

## Entering and leaving compiled code

A generated function is `uint32_t fn(uint32_t pc)`:

- `pc` is where to enter: the function's start or any other block leader. The
  function begins with a `switch (pc)` over all of them. Every block leader
  is an entry, because any block can be where an interrupt returns to.
- The return value is the PC at which execution continues.

Calls and returns:

- `jal` to a compiled function is a C++ call: `PS2N_CALL(fn, target, ret)`.
  When the callee returns, the caller compares the returned PC with `ret`. If
  they are equal it carries on. If not, it returns that PC in turn; so does
  its caller; the dispatcher ends up with a PC and continues there.
- `jr $ra` is `return target`.
- A branch or jump into another function is a tail call (`PS2N_JUMP`,
  `[[clang::musttail]]`).
- `jalr` and `jal` to an address without compiled code go through
  `ps2n::ee::dispatch(pc)`, which runs compiled code if there is some and
  otherwise interprets until compiled code is reached again.
- `jr` through another register sets `pc` and goes back to the function's own
  entry switch (a switch table lands on one of its blocks); the default case
  is `ps2n::ee::jump(pc)`: a tail call if `pc` is the start of a compiled
  function, else `return pc`.

So every way out of the usual call/return shape (an interrupt, a system call,
a thread switch by the kernel, `longjmp`, a return to an address nobody is
waiting for) is the same mechanism: a PC travels up the host stack until a
frame recognizes it, or reaches the dispatcher. `kUnwind` (1) is returned
instead of a PC when every frame must give up; the dispatcher then continues
at `cpuRegs.pc`.

The interpreter takes over in the middle of a block through
`ps2n::ee::leave(pc, eighths)`: it finishes the block, counting from
`eighths`. The recompiler emits it for what it does not translate: an unknown
encoding; a branch whose delay slot holds a branch, a system instruction or a
`Config` write; a block that runs for more than 64 instructions past its
function's end. For a likely branch with such a delay slot (GCC's division
check, `beql divisor, $zero, 1f; break 7`) only the taken path leaves.

A function whose last instruction is in the middle of a block continues
through the instructions that follow it, compiled a second time, to the end
of that block ("tail"). Its `extent` in the function table then reaches past
its `end`, and the verification below covers the extent.

## Events, system calls, threads

- `PS2N_BLOCK_END` calls `ps2n::ee::eventTest(next)` when an event is due. If
  the core took an interrupt, `cpuRegs.pc` is the exception vector; the value
  returned differs from `next`; every compiled frame returns; the dispatcher
  interprets the BIOS's handler. When the kernel returns with `ERET`, the
  dispatcher finds the entry for that PC (a block leader of the interrupted
  function, or of another thread's function) and enters it.
- `SYSCALL` and the other system instructions are carried out by PCSX2's own
  function for them (`PS2N_SYSTEM`). A system call therefore always leaves
  compiled code; the program is re-entered at the instruction after it.
- Nothing is high-level emulated. Threads, semaphores, alarms, handlers are
  the BIOS kernel's, running interpreted.

The host stack: a guest call is a host call, so the thread that runs the core
needs a large stack (64 MB in `ps2n-host`). Guest code can make the host stack
grow without its own growing (a callee that jumps back into its caller). At
each event test reached from compiled code the provider checks the depth
against the budget the host gave it (`SetHostStackBudget`) and returns
`kUnwind` past it. `ps2n-host -stack-budget 0` does that at every event test;
the `ps2autotests/lockstep-no-stack/*` tests run that way.

## Is the code in memory the code that was compiled?

Each function has a state (`FunctionState`): unverified, trusted, invalid, or
"variant n is in memory".

- `PS2N_ENTER` at a function's top passes if the state is trusted. Otherwise
  `enterSlow` compares a 64-bit hash of the words in memory over the
  function's extent with the hash the recompiler stored. A generated program
  thus carries fingerprints of the game's code, not the code.
- Equal: the function's pages are write-protected through the core
  (`mmap_MarkCountedRamPage`) and the state becomes trusted.
- Different: the state becomes invalid, the code is interpreted, and both its
  block starts and the words found in memory are recorded for the recompiler
  (see "Variants" below).
- When something writes to a protected page, the core's page-fault handler
  calls the provider's `Clear`. The functions in that range go back to
  unverified, to be compared again at their next entry, and compiled frames
  are abandoned at the next event test.
- A function whose pages keep being written (it shares a page with data) is
  protected 16 times at most; after that it is compared on every entry.

### Variants

A program that rewrites a function, or a patch applied to memory, puts other
code where a function was compiled. The first build of a program cannot know
it. Its runtime notices, interprets, and writes what it found to the capture
file (`ps2n-host -capture`): the words over the function's own range.

Given that file (`ps2n-recomp compile --capture`), the recompiler compiles
that code too, as a *variant* of the function: same range, same block
leaders, its own hash. At run time whichever of them matches memory runs.
Other compiled code always calls the function itself; when a variant is in
memory, the function's entry check fails and `enterSlow` runs the variant in
its place. The dispatcher goes to the right one directly.

`tests/programs/selfmod.s` is such a program. `programs/first-build/selfmod`
checks that a build from the executable alone reports exactly what
`selfmod.capture.toml` holds; `programs/rebuilt/selfmod` checks that the
build with it interprets nothing of the program.

Not covered yet: code where no function was compiled at all (an overlay
loaded into memory the executable does not fill). It is interpreted and its
block starts are reported, but its words are not captured.

No event test is forced when code memory is written: the core runs the IOP
and its counters from event tests, so an extra one could make a compiled run
differ from an interpreted one. Until the next event test a frame already
inside a changed function can still run the old code, as the EE's instruction
cache lets it on a console; a program must flush the cache to see new code,
and that is a system call.

Known gap: once a function is compared on every entry, a change to its code
while it is already running is not seen until it is entered again.

## What is not compiled, and how it shows

Nothing is interpreted or delegated silently. `ps2n-host` prints after a run:

- how many instructions the interpreter ran, and how many of them inside the
  executable's code range (should be zero once the image is complete);
- how many were *delegated*: carried out by PCSX2's interpreter function for
  the instruction because PS2Native has no semantics of its own for it yet.
  Compiled code delegates the same instructions (`ps2n::ee::delegate`); the
  recompiler reports how many it emitted;
- the block starts inside the code range that had to be interpreted
  ("missing entries"), written to a file with `-capture <file>`. Giving that
  file to `ps2n-recomp compile --capture` makes them block leaders in the
  next build.

PS2Native's own semantics so far: integer arithmetic and logic, shifts,
multiply and divide, HI/LO, the shift-amount register, loads and stores of
general registers ([ops_int.h](../runtime/include/ps2n/ops_int.h)); the FPU
([ops_fpu.h](../runtime/include/ps2n/ops_fpu.h)); the multimedia instructions
([ops_mmi.h](../runtime/include/ps2n/ops_mmi.h)); of COP2, the two transfers
of VU0's integer and control registers, `CFC2` and `CTC2`
([ops_cop2.h](../runtime/include/ps2n/ops_cop2.h)). Delegated: COP0, every
other VU0 macro instruction (COP2), `CACHE`, `SYNC`, `PREF`.

Which behavior a function reproduces is stated at the top of its header. The
rule ([ADR 0005](adr/0005-never-further-from-the-console-than-pcsx2.md)):
PCSX2's recompiler, which games are tested against; the console where a
recording shows the recompiler wrong and PCSX2's interpreter right, or where
the recompiler has no single behavior; never a behavior that neither has.
`ps2n-host -selftest` compares every function with both PCSX2
implementations on random and boundary operands, and lists what it
deliberately does not compare, with the reason in `host/selftest.cpp`. The
`ps2autotests/never-further/*` tests hold the outputs of whole programs to
the same rule, line by line against stock PCSX2 and the console.

A corrected instruction can cost a program its match with the console, when
the old behavior was hiding a mistake elsewhere. That happened with `CTC2`
(proof-of-concept log, increment 5). Find out which before choosing: the
rule decides the instruction, not the number of programs that match.

To add a family: write `ops_<family>.h`; put its rows in a table of their
own (`core/include/ps2n/r5900_sem_*.inc`) and include that in `ExecuteOwn()`
(`runtime/ee_provider.cpp`), in `semanticCall()`
(`recompiler/src/codegen.cpp`) and in `kOwnOps` (`host/selftest.cpp`); give
the self-test the state the family touches (`State` in `host/selftest.cpp`);
run the self-test and explain every difference before excluding it; then
plant bugs to see that it reports them. A deep run:
`cmake -DSELFTEST=1 -DSELFTEST_TRIALS=20000 -DSECONDS=900 ... -P tests/guest_test.cmake`.

The FPU functions compute under the host's FP control register as PCSX2 sets
it for the EE (rounding toward zero, denormals as zero). The C++ compiler
knows nothing about that, so every operand passes through `fpu::opaque()`,
which hides the value and fixes the point of computation. Do not remove it.
PCSX2's clamp mode Full is not implemented: with it set, the provider warns,
runs no compiled code, and delegates the FPU.

## Comparing two runs

Event tests happen at the same instructions and cycle counts whether a block
was compiled or interpreted. The provider folds the registers at every event
test into a fingerprint of the run:

- `ps2n-host -trace <file>` writes one line per event test: number, PC,
  cycle count, fingerprint so far. `-trace-detail <n>` adds the registers
  from event test `n` on.
- `ps2n-host -trace-against <file>` compares a run with an earlier run's
  file as it goes, and stops at the first event test that differs.

The `*/lockstep/*` tests run every test program interpreted and then compiled
this way. When one fails, the event test number it reports is where to look:
rerun both with `-trace-detail` a little before it and compare the registers.

That only compares two runs of PS2Native's provider. Against stock PCSX2,
which has no such record, there is the program's output and when it came:
`ps2n-host -times <file>` (the test driver's `TIMES=1`) writes every line
the program prints with the number of frames shown and the EE's cycle count
at that moment. Two such files side by side show between which two lines the
runs part in time. The proof-of-concept log, "When the same output comes at
another time", has two cases found that way.

## The recompiler's analysis

`recompiler/src/analysis.cpp`. Function starts come from symbols, the entry
point, `jal` targets, code addresses found in data or built by `lui`/`addiu`
pairs, and a prologue heuristic; code no function claims becomes functions of
its own. **Function boundaries only affect speed**: a wrong one turns a jump
into a call between two C++ functions, or makes a function run on into the
next.

What has to be right is the set of block leaders: a function's start; every
branch or jump target; the instruction after a branch's or jump's delay slot;
the instruction after a system instruction; code addresses found in data
(switch tables); and entries captured from an earlier run. Padding after a
jump or return is skipped. An address that is none of these and is still
jumped to at run time is interpreted and reported.
