# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

"""Derive APlayerController::GetPlayerViewPoint's RVA from a UE4 shipping EXE.

UE4 disables RTTI for engine classes, and GetPlayerViewPoint is not a UFUNCTION
in 4.27, so neither a type name nor a reflection name points at it directly.
GetActorEyesViewPoint IS a UFUNCTION, though, and AController::GetPlayerViewPoint
is a one-line forwarder to it. That gives a chain that needs no symbols:

  A. "GetActorEyesViewPoint" string -> the FNameNativePtrPair that registers it
     ({const char* name; FNativeFuncPtr exec;}) -> execGetActorEyesViewPoint.
  B. The exec thunk ends in a virtual call, `call qword ptr [rax + SLOT]`,
     which is AActor's vtable slot for GetActorEyesViewPoint.
  C. AController::GetPlayerViewPoint is the tail-jump forwarder
     `mov rax,[rcx]; jmp qword ptr [rax + SLOT]` - a 9-byte signature.
  D. Whichever vtable slot holds that forwarder is GetPlayerViewPoint's slot.
     Every other value at that slot is an override; APlayerController's is the
     one that reads the camera cache, and that is the hook target.

Run: python scripts/derive_gpv.py <path-to-exe>
"""

import struct
import sys
from collections import Counter, OrderedDict

import capstone
import pefile


def load(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories()
    base = pe.OPTIONAL_HEADER.ImageBase
    # One flat image buffer indexed by RVA, so pointer chasing is a slice.
    image = bytearray(pe.OPTIONAL_HEADER.SizeOfImage)
    for s in pe.sections:
        data = s.get_data()
        image[s.VirtualAddress:s.VirtualAddress + len(data)] = data
    sections = {}
    for s in pe.sections:
        name = s.Name.decode('ascii', 'ignore').rstrip('\0')
        sections[name] = (s.VirtualAddress, s.Misc_VirtualSize)
    return pe, base, bytes(image), sections


def qword(image, rva):
    if rva < 0 or rva + 8 > len(image):
        return 0
    return struct.unpack_from('<Q', image, rva)[0]


def find_all(hay, needle, start=0, end=None):
    end = len(hay) if end is None else end
    out = []
    i = hay.find(needle, start, end)
    while i != -1:
        out.append(i)
        i = hay.find(needle, i + 1, end)
    return out


def main():
    path = sys.argv[1]
    pe, base, image, sections = load(path)
    text_rva, text_size = sections['.text']
    text_lo, text_hi = text_rva, text_rva + text_size

    def in_text(va):
        return base + text_lo <= va < base + text_hi

    print('image base 0x%X  .text rva [0x%X, 0x%X)' % (base, text_lo, text_hi))

    # ---- A. reflection name -> exec thunk -------------------------------
    name = b'GetActorEyesViewPoint\0'
    name_rvas = [r for r in find_all(image, name) if not (text_lo <= r < text_hi)]
    print('\nA. "GetActorEyesViewPoint" at %d non-.text location(s): %s'
          % (len(name_rvas), ', '.join('0x%08X' % r for r in name_rvas)))

    exec_thunks = OrderedDict()
    for nr in name_rvas:
        ptr = struct.pack('<Q', base + nr)
        for pr in find_all(image, ptr):
            if pr % 8:
                continue
            exec_va = qword(image, pr + 8)
            ok = in_text(exec_va)
            print('   ptr @ 0x%08X -> pair.exec = 0x%08X %s'
                  % (pr, exec_va - base if ok else exec_va, '' if ok else '(not .text)'))
            if ok:
                exec_thunks[exec_va - base] = pr

    if not exec_thunks:
        print('!! no exec thunk found - the registration shape differs, stop here')
        return 1

    # ---- B. exec thunk -> vtable slot -----------------------------------
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    slots = Counter()
    print('\nB. virtual-call displacements inside the exec thunk(s)')
    for thunk in exec_thunks:
        code = image[thunk:thunk + 0x400]
        for ins in md.disasm(code, base + thunk):
            if ins.mnemonic in ('call', 'jmp') and '[' in ins.op_str:
                op = ins.operands[0]
                # RIP-relative is an import/global call, not a vtable dispatch.
                if (op.type == capstone.x86.X86_OP_MEM
                        and op.mem.base not in (0, capstone.x86.X86_REG_RIP)
                        and op.mem.disp > 0):
                    slots[op.mem.disp] += 1
                    print('   0x%08X: %-6s %-30s -> slot 0x%X'
                          % (ins.address - base, ins.mnemonic, ins.op_str, op.mem.disp))
            # Only `ret` ends the thunk: an unconditional jmp here is ordinary
            # intra-function control flow around the two out-param branches.
            if ins.mnemonic == 'ret':
                break

    if not slots:
        print('!! no virtual dispatch in the thunk')
        return 1

    # ---- C. the AController forwarder -----------------------------------
    # mov rax,[rcx]  =  48 8B 01
    # jmp qword ptr [rax+disp32] = FF A0 <disp32>
    print('\nC. forwarder `mov rax,[rcx]; jmp qword ptr [rax+slot]`')
    forwarders = OrderedDict()
    for slot in slots:
        sig = b'\x48\x8B\x01\xFF\xA0' + struct.pack('<I', slot)
        for r in find_all(image, sig, text_lo, text_hi):
            forwarders[r] = slot
            print('   forwarder @ rva 0x%08X (slot 0x%X)' % (r, slot))
        # Same body, but reached with the `this` already in rax.
        sig2 = b'\x48\x8B\x01\x48\xFF\xA0' + struct.pack('<I', slot)
        for r in find_all(image, sig2, text_lo, text_hi):
            forwarders[r] = slot
            print('   forwarder @ rva 0x%08X (slot 0x%X, REX.W jmp)' % (r, slot))

    if not forwarders:
        print('!! no forwarder matched - AController::GetPlayerViewPoint may be inlined')
        return 1

    # ---- vtable harvest --------------------------------------------------
    # Runs of >= 8 consecutive .text pointers outside .text.
    print('\nharvesting vtables...')
    vtables = []
    for sec_name, (sec_rva, sec_size) in sections.items():
        if sec_name == '.text':
            continue
        start = (sec_rva + 7) & ~7
        end = sec_rva + sec_size
        i = start
        while i + 8 <= end:
            if not in_text(qword(image, i)):
                i += 8
                continue
            j = i
            while j + 8 <= end and in_text(qword(image, j)):
                j += 8
            if (j - i) // 8 >= 8:
                vtables.append((i, (j - i) // 8))
            i = j + 8
    print('   %d vtable candidates' % len(vtables))

    # ---- D. which slot holds a forwarder = the GPV slot -----------------
    print('\nD. vtable slots holding a forwarder')
    gpv_slots = Counter()
    fwd_vas = {base + r for r in forwarders}
    for vt_rva, n in vtables:
        for k in range(n):
            if qword(image, vt_rva + 8 * k) in fwd_vas:
                gpv_slots[8 * k] += 1
    for slot, count in gpv_slots.most_common():
        print('   slot 0x%X in %d vtable(s)' % (slot, count))

    if not gpv_slots:
        print('!! the forwarder is in no vtable')
        return 1

    # ---- E. overrides at that slot --------------------------------------
    for slot, _ in gpv_slots.most_common():
        targets = Counter()
        idx = slot // 8
        for vt_rva, n in vtables:
            if n <= idx:
                continue
            va = qword(image, vt_rva + 8 * idx)
            if in_text(va):
                targets[va] += 1
        print('\nE. slot 0x%X: %d distinct targets' % (slot, len(targets)))
        for va, count in targets.most_common():
            rva = va - base
            tag = ' <- AController forwarder' if va in fwd_vas else ''
            print('   0x%08X  in %4d vtable(s)%s' % (rva, count, tag))
    return 0


if __name__ == '__main__':
    sys.exit(main())
