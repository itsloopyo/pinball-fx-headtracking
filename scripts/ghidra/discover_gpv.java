// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Pin APlayerController::GetPlayerViewPoint in a stripped UE 4.27 shipping EXE.
//
// GetPlayerViewPoint itself is not a UFUNCTION in 4.27, so its name is not in
// the binary. GetActorEyesViewPoint IS one, and AController::GetPlayerViewPoint
// is a one-line forwarder to it, so the reflection name anchors the whole
// chain:
//
//   A. "GetActorEyesViewPoint" string -> the FNameNativePtrPair that registers
//      it {const char* name; FNativeFuncPtr exec;} -> execGetActorEyesViewPoint.
//   B. That exec thunk ends in a VIRTUAL call, `call qword ptr [rax + SLOT]`,
//      which hands us AActor's vtable slot for GetActorEyesViewPoint.
//   C. AController::GetPlayerViewPoint is the tiny forwarder whose whole body
//      is a virtual dispatch through that same SLOT.
//   D. Whichever vtable slot holds that forwarder is GetPlayerViewPoint's slot.
//      Every other value at that slot across all vtables is an override; the
//      APlayerController one is the camera-cache reader we want to hook.
//
// Everything is reported as RVAs, ready to paste into steam_offsets.cpp.
//
// The report is written to the path given as the script's first argument:
//
//   analyzeHeadless <proj-dir> <proj> -process <exe> \
//       -scriptPath scripts/ghidra -postScript discover_gpv.java <out-file>
//
// Point it at your own RE scratch directory, which is gitignored: decompiler
// output belongs on disk, never in this repository.
import java.io.PrintWriter;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

public class discover_gpv extends GhidraScript {

    long base;
    long textLo, textHi;
    Memory mem;
    AddressSpace space;
    PrintWriter out;

    boolean isText(long p) { return p >= textLo && p < textHi; }
    long rva(long a) { return a - base; }
    Address addr(long a) { return space.getAddress(a); }

    @Override
    public void run() throws Exception {
        base = currentProgram.getImageBase().getOffset();
        mem = currentProgram.getMemory();
        space = currentProgram.getAddressFactory().getDefaultAddressSpace();

        MemoryBlock text = null;
        for (MemoryBlock b : mem.getBlocks()) {
            if (".text".equals(b.getName())) text = b;
        }
        if (text == null) { println("no .text"); return; }
        textLo = text.getStart().getOffset();
        textHi = text.getEnd().getOffset() + 1;

        String[] args = getScriptArgs();
        if (args.length < 1 || args[0].isEmpty()) {
            println("usage: discover_gpv.java <output-file>");
            return;
        }
        String outPath = args[0];

        out = new PrintWriter(outPath);
        out.printf("image base 0x%x  .text [0x%x, 0x%x)  rva .text [0x%x, 0x%x)%n%n",
            base, textLo, textHi, rva(textLo), rva(textHi));

        // ---- A: the reflection name -> exec thunk ------------------------
        List<Long> nameAddrs = findAsciiString("GetActorEyesViewPoint");
        out.printf("A. \"GetActorEyesViewPoint\" at %d address(es)%n", nameAddrs.size());
        Set<Long> execThunks = new LinkedHashSet<>();
        for (long na : nameAddrs) {
            out.printf("   string @ rva 0x%08x%n", rva(na));
            for (long ptr : findQwordRefs(na)) {
                long exec = readQword(ptr + 8);
                out.printf("     ptr @ rva 0x%08x -> pair.exec = 0x%08x %s%n",
                    rva(ptr), rva(exec), isText(exec) ? "" : "(NOT .text - skipped)");
                if (isText(exec)) execThunks.add(exec);
            }
        }

        // ---- B: exec thunk -> vtable slot --------------------------------
        Set<Long> eyesSlots = new LinkedHashSet<>();
        out.printf("%nB. virtual-call slots inside the exec thunk(s)%n");
        for (long t : execThunks) {
            for (long slot : virtualCallSlots(t)) {
                out.printf("   exec 0x%08x -> call [reg + 0x%x]%n", rva(t), slot);
                eyesSlots.add(slot);
            }
        }

        // ---- vtable harvest ---------------------------------------------
        List<long[]> vtables = harvestVtables();   // {startAddr, lengthInSlots}
        out.printf("%nvtable candidates: %d%n", vtables.size());

        // ---- C: AController::GetPlayerViewPoint (the forwarder) ----------
        out.printf("%nC. forwarder candidates (whole body = virtual dispatch through an eyes slot)%n");
        Map<Long, Integer> forwarders = new LinkedHashMap<>();
        for (long slot : eyesSlots) {
            for (long fn : distinctSlotTargets(vtables, slot)) {
                // not the forwarder itself - we want callers, handled below
            }
        }
        // A forwarder is short and its only control transfer is jmp/call [reg+slot].
        for (long slot : eyesSlots) {
            for (long fn : shortVirtualForwarders(slot)) {
                forwarders.put(fn, (int) slot);
                out.printf("   forwarder @ rva 0x%08x  (dispatches [reg + 0x%x])%n", rva(fn), slot);
            }
        }

        // ---- D: which vtable slot holds a forwarder = GPV's slot ---------
        out.printf("%nD. vtable slots holding a forwarder -> GetPlayerViewPoint slot%n");
        Map<Long, Integer> gpvSlotCounts = new LinkedHashMap<>();
        for (long[] vt : vtables) {
            for (int i = 0; i < vt[1]; i++) {
                long v = readQword(vt[0] + 8L * i);
                if (forwarders.containsKey(v))
                    gpvSlotCounts.merge((long) (8 * i), 1, Integer::sum);
            }
        }
        for (Map.Entry<Long, Integer> e : gpvSlotCounts.entrySet())
            out.printf("   slot 0x%x seen in %d vtable(s)%n", e.getKey(), e.getValue());

        // ---- E: every override at that slot, decompiled ------------------
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();

        for (long slot : gpvSlotCounts.keySet()) {
            Set<Long> targets = distinctSlotTargets(vtables, slot);
            out.printf("%nE. slot 0x%x has %d distinct targets%n", slot, targets.size());
            for (long t : targets) {
                if (forwarders.containsKey(t)) {
                    out.printf("   0x%08x  (base forwarder - AController::GetPlayerViewPoint)%n", rva(t));
                    continue;
                }
                out.printf("%n   ==== override @ rva 0x%08x ====%n", rva(t));
                Function fn = fm.getFunctionAt(addr(t));
                if (fn == null) { disassemble(addr(t)); fn = createFunction(addr(t), null); }
                if (fn == null) { out.println("   (could not create function)"); continue; }
                DecompileResults res = di.decompileFunction(fn, 90, monitor);
                if (res != null && res.getDecompiledFunction() != null)
                    out.println(res.getDecompiledFunction().getC());
            }
        }
        di.dispose();
        out.close();
        println("wrote " + outPath);
    }

    // ---------------- helpers ----------------

    List<Long> findAsciiString(String s) throws Exception {
        List<Long> hits = new ArrayList<>();
        byte[] pat = (s + "\0").getBytes("US-ASCII");
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            Address a = b.getStart();
            while (true) {
                Address f = mem.findBytes(a, b.getEnd(), pat, null, true, monitor);
                if (f == null) break;
                hits.add(f.getOffset());
                a = f.add(1);
            }
        }
        return hits;
    }

    // Every 8-byte-aligned qword in an initialized non-exec block equal to `target`.
    List<Long> findQwordRefs(long target) throws Exception {
        List<Long> hits = new ArrayList<>();
        byte[] pat = new byte[8];
        for (int i = 0; i < 8; i++) pat[i] = (byte) ((target >>> (8 * i)) & 0xFF);
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            Address a = b.getStart();
            while (true) {
                Address f = mem.findBytes(a, b.getEnd(), pat, null, true, monitor);
                if (f == null) break;
                if ((f.getOffset() & 7) == 0) hits.add(f.getOffset());
                a = f.add(1);
            }
        }
        return hits;
    }

    long readQword(long a) {
        try { return mem.getLong(addr(a)); } catch (Exception e) { return 0; }
    }

    // Displacements of `call/jmp qword ptr [reg + disp]` inside a function.
    List<Long> virtualCallSlots(long fnAddr) {
        List<Long> slots = new ArrayList<>();
        Function fn = currentProgram.getFunctionManager().getFunctionAt(addr(fnAddr));
        if (fn == null) { try { disassemble(addr(fnAddr)); fn = createFunction(addr(fnAddr), null); } catch (Exception e) { } }
        if (fn == null) return slots;
        InstructionIterator it = currentProgram.getListing().getInstructions(fn.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            String m = ins.getMnemonicString().toLowerCase();
            if (!m.startsWith("call") && !m.startsWith("jmp")) continue;
            String rep = ins.toString().toLowerCase();
            if (!rep.contains("[")) continue;
            for (int i = 0; i < ins.getNumOperands(); i++) {
                for (Object o : ins.getOpObjects(i)) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (v > 0 && v < 0x4000 && (v & 7) == 0) slots.add(v);
                    }
                }
            }
        }
        return slots;
    }

    // Short functions whose only control transfer dispatches through `slot`.
    List<Long> shortVirtualForwarders(long slot) {
        List<Long> hits = new ArrayList<>();
        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        while (fit.hasNext()) {
            Function fn = fit.next();
            long len = fn.getBody().getNumAddresses();
            if (len == 0 || len > 48) continue;      // forwarder is a handful of instructions
            boolean hitSlot = false, otherTransfer = false;
            InstructionIterator it = currentProgram.getListing().getInstructions(fn.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                String m = ins.getMnemonicString().toLowerCase();
                if (!m.startsWith("call") && !m.startsWith("jmp")) continue;
                boolean matched = false;
                for (int i = 0; i < ins.getNumOperands(); i++)
                    for (Object o : ins.getOpObjects(i))
                        if (o instanceof Scalar && ((Scalar) o).getUnsignedValue() == slot) matched = true;
                if (matched) hitSlot = true; else otherTransfer = true;
            }
            if (hitSlot && !otherTransfer) hits.add(fn.getEntryPoint().getOffset());
        }
        return hits;
    }

    // Runs of >= 8 consecutive .text pointers in a non-exec block.
    List<long[]> harvestVtables() {
        List<long[]> vts = new ArrayList<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            long start = b.getStart().getOffset();
            long end = b.getEnd().getOffset() + 1;
            start = (start + 7) & ~7L;
            long i = start;
            while (i + 8 <= end) {
                if (!isText(readQword(i))) { i += 8; continue; }
                long j = i;
                while (j + 8 <= end && isText(readQword(j))) j += 8;
                long slots = (j - i) / 8;
                if (slots >= 8) vts.add(new long[]{i, slots});
                i = j + 8;
            }
        }
        return vts;
    }

    Set<Long> distinctSlotTargets(List<long[]> vtables, long slot) {
        Set<Long> targets = new LinkedHashSet<>();
        int idx = (int) (slot / 8);
        for (long[] vt : vtables) {
            if (vt[1] <= idx) continue;
            long v = readQword(vt[0] + 8L * idx);
            if (isText(v)) targets.add(v);
        }
        return targets;
    }
}
