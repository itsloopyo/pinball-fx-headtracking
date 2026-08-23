// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Pre-script (Java; Ghidra 12 dropped Jython): switch off the decompiler-based
// analyzers before auto-analysis. PinballFX-Win64-Shipping.exe is a 102 MB UE
// 4.27 shipping build with a ~69 MB .text; the decompiler-dependent analyzers
// ("Decompiler Parameter ID", "Decompiler Switch Analysis", "Call Convention
// ID") multiply that into many hours. The discovery scripts here need only
// disassembly, RTTI vftable symbols and string/data xrefs, none of which
// depend on those analyzers, so turning them off takes the import from hours
// to minutes. Individual functions are still decompiled on demand via
// DecompInterface in the post-scripts.
import ghidra.app.script.GhidraScript;
import ghidra.framework.options.Options;
import ghidra.program.model.listing.Program;

public class disable_heavy_analyzers extends GhidraScript {
    public void run() throws Exception {
        Options opts = currentProgram.getOptions(Program.ANALYSIS_PROPERTIES);
        String[] kill = {"Decompiler", "Switch", "Parameter ID", "Call Convention"};
        for (String name : opts.getOptionNames()) {
            if (name.contains(".")) continue;          // skip sub-options
            boolean hit = false;
            for (String k : kill) if (name.contains(k)) { hit = true; break; }
            if (!hit) continue;
            try {
                opts.setBoolean(name, false);
                println("disabled analyzer: " + name);
            } catch (Exception e) {
                println("could not disable " + name + ": " + e);
            }
        }
    }
}
