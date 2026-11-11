# SPDX-License-Identifier: GPL-3.0-or-later
#
# Programs of tests/sdk whose output differs from what stock PCSX2 prints with
# its recompilers (the recorded <name>.expected), per CPU configuration. These
# tests still run; CTest expects them to fail and reports it when one starts
# to match.
#
# Each list is what a run found. Do not add a name to make a failure go away:
# find out why it differs first.
#
# Variables are named SDK_DIFFERS_<configuration>.

# Stock PCSX2 v2.9.170 with its interpreters, BIOS SCPH-70004 v2.00, 2026-11-10.
set(SDK_DIFFERS_stock-interp
	vu      # the VU0 status flags after DIV: 00e3 where PCSX2's VU recompiler gives 08e3.
	        # PCSX2's two VU implementations disagree; the EE has no part in it
)
