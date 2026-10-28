// Exports Ghidra's function boundaries for PS2Native.
//
// Writes a CSV ("Name,Start,End,Size", the same layout PS2Recomp uses) and,
// optionally, a starter config.toml that points at it. Point
// [input].ghidra_map at the CSV and recompile: Ghidra's boundaries then take
// priority over the recompiler's own heuristics, which matters most for
// stripped retail executables.
//
// GUI:      Script Manager -> PS2Native -> ExportPs2nFunctions.java
// Headless: analyzeHeadless <project_dir> <project> -process <game.elf> \
//               -postScript ExportPs2nFunctions.java out/functions.csv [out/config.toml]
//
//@category PS2Native
//@menupath Tools.PS2Native.Export Function Map

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.mem.MemoryBlock;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

public class ExportPs2nFunctions extends GhidraScript {

    private static final class Entry {
        final String name;
        final long start;
        final long end;

        Entry(String name, long start, long end) {
            this.name = name;
            this.start = start;
            this.end = end;
        }
    }

    @Override
    protected void run() throws Exception {
        File csv;
        File toml = null;
        String[] args = getScriptArgs();
        if (args.length > 0) {
            csv = new File(args[0]);
            if (args.length > 1) {
                toml = new File(args[1]);
            }
        } else {
            csv = askFile("Function map (CSV) to write", "Export");
            if (askYesNo("PS2Native", "Also write a starter config.toml next to the CSV?")) {
                toml = new File(csv.getParentFile(), "config.toml");
            }
        }

        List<Entry> entries = new ArrayList<>();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Function f = it.next();
            if (f.isExternal()) {
                continue;
            }
            Address entry = f.getEntryPoint();
            MemoryBlock block = currentProgram.getMemory().getBlock(entry);
            if (block == null || !block.isExecute()) {
                continue;
            }
            // Use the contiguous piece of the body that holds the entry point:
            // the recompiler wants [start, end) ranges.
            AddressRange range = f.getBody().getRangeContaining(entry);
            long start = entry.getOffset();
            long end = range != null ? range.getMaxAddress().getOffset() + 1 : start + 4;
            entries.add(new Entry(f.getName(), start, end));
        }
        entries.sort(Comparator.comparingLong(e -> e.start));

        try (PrintWriter w = new PrintWriter(csv, "UTF-8")) {
            w.println("# PS2Native function map exported from Ghidra (" + currentProgram.getName() + ")");
            w.println("Name,Start,End,Size");
            for (Entry e : entries) {
                w.printf("%s,0x%08X,0x%08X,%d%n", csvField(e.name), e.start, e.end, e.end - e.start);
            }
        }
        println("PS2Native: wrote " + entries.size() + " functions to " + csv.getAbsolutePath());

        if (toml != null) {
            String elf = currentProgram.getExecutablePath();
            String name = currentProgram.getName().replaceAll("[^A-Za-z0-9_]", "_");
            try (PrintWriter w = new PrintWriter(toml, "UTF-8")) {
                w.println("# Starter config written by ExportPs2nFunctions.java. See docs/config.md.");
                w.println();
                w.println("[input]");
                w.println("elf = " + tomlString(elf));
                w.println("ghidra_map = " + tomlString(csv.getAbsolutePath()));
                w.println();
                w.println("[output]");
                w.println("dir = " + tomlString("out/" + name));
                w.println("name = " + tomlString(name));
                w.println("mode = \"multi\"");
                w.println();
                w.println("[analysis]");
                w.println("# entries_files = [\"ps2n_missing.toml\"]   # written by the runtime");
                w.println();
                w.println("[stubs]");
                w.println("auto = true");
                w.println();
                w.println("[functions]");
                w.println("# skip = [\"some_function\"]");
            }
            println("PS2Native: wrote " + toml.getAbsolutePath());
        }
    }

    private static String csvField(String s) {
        if (s.contains(",") || s.contains("\"")) {
            return "\"" + s.replace("\"", "\"\"") + "\"";
        }
        return s;
    }

    private static String tomlString(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }
}
