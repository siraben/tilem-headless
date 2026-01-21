#!/usr/bin/env python3
import argparse
import struct
import sys

MAGIC = b"TLMT"
HEADER_FMT = "<4sHHIII"
HEADER_SIZE = struct.calcsize(HEADER_FMT)

INSTR_FMT = "<III" + "H" * 15 + "BBBBB"
INSTR_SIZE = struct.calcsize(INSTR_FMT)

MEM_WRITE_FMT = "<IB"
MEM_WRITE_SIZE = struct.calcsize(MEM_WRITE_FMT)

KEY_EVENT_FMT = "<BBIH"
KEY_EVENT_SIZE = struct.calcsize(KEY_EVENT_FMT)

IDX_PC = 0
IDX_OPCODE = 1
IDX_CLOCK = 2
IDX_AF = 3
IDX_BC = 4
IDX_DE = 5
IDX_HL = 6
IDX_IX = 7
IDX_IY = 8
IDX_SP = 9
IDX_PC_REG = 10
IDX_IR = 11
IDX_WZ = 12
IDX_WZ2 = 13
IDX_AF2 = 14
IDX_BC2 = 15
IDX_DE2 = 16
IDX_HL2 = 17
IDX_IFF1 = 18
IDX_IFF2 = 19
IDX_IM = 20
IDX_R7 = 21
IDX_HALTED = 22

RET_COND = {0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8}
RET_UNCOND = {0xC9}
CALL_COND = {0xC4, 0xCC, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC}
CALL_UNCOND = {0xCD}
RST_SET = {0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF}
JP_COND = {0xC2, 0xCA, 0xD2, 0xDA, 0xE2, 0xEA, 0xF2, 0xFA}
JP_UNCOND = {0xC3}
JP_INDIRECT = {0xE9}
JR_COND = {0x20, 0x28, 0x30, 0x38}
JR_UNCOND = {0x18}
DJNZ = {0x10}
ED_RET = {0x45, 0x4D, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D}


def opcode_prefix(opcode):
    if opcode & 0xFF000000 in (0xDDCB0000, 0xFDCB0000):
        return opcode & 0xFF000000
    prefix = opcode & 0xFF00
    if prefix in (0xDD00, 0xFD00, 0xCB00, 0xED00):
        return prefix
    return 0


def control_flow_info(opcode):
    prefix = opcode_prefix(opcode)
    low = opcode & 0xFF
    base_len = 1
    kind = None
    conditional = False

    if prefix in (0xCB00, 0xDDCB0000, 0xFDCB0000):
        return None

    if prefix == 0xED00 and low in ED_RET:
        return {"kind": "ret", "len": 2, "conditional": False}
    if prefix == 0xED00:
        return None

    if low in RET_UNCOND:
        kind = "ret"
    elif low in RET_COND:
        kind = "ret"
        conditional = True
    elif low in CALL_UNCOND:
        kind = "call"
        base_len = 3
    elif low in CALL_COND:
        kind = "call"
        conditional = True
        base_len = 3
    elif low in RST_SET:
        kind = "rst"
    elif low in JP_UNCOND:
        kind = "jp"
        base_len = 3
    elif low in JP_COND:
        kind = "jp"
        conditional = True
        base_len = 3
    elif low in JP_INDIRECT:
        kind = "jp"
    elif low in JR_UNCOND:
        kind = "jr"
        base_len = 2
    elif low in JR_COND:
        kind = "jr"
        conditional = True
        base_len = 2
    elif low in DJNZ:
        kind = "djnz"
        conditional = True
        base_len = 2
    else:
        return None

    if prefix in (0xDD00, 0xFD00):
        base_len += 1

    return {"kind": kind, "len": base_len, "conditional": conditional}


def format_flow(event):
    if event["type"] == "call":
        return (
            f"{event['idx']:>8} CALL 0x{event['from']:04x} -> 0x{event['to']:04x} "
            f"ret=0x{event['ret']:04x} depth={event['depth']}"
        )
    if event["type"] == "ret":
        suffix = ""
        if event.get("mismatch"):
            suffix = f" mismatch ret=0x{event['ret']:04x}"
        return (
            f"{event['idx']:>8} RET  0x{event['from']:04x} -> 0x{event['to']:04x} "
            f"depth={event['depth']}{suffix}"
        )
    if event["type"] == "jump":
        taken = "taken" if event["taken"] else "not-taken"
        return (
            f"{event['idx']:>8} JUMP {taken} 0x{event['from']:04x} -> 0x{event['to']:04x} "
            f"kind={event['kind']}"
        )
    if event["type"] == "async":
        return (
            f"{event['idx']:>8} ASYNC 0x{event['from']:04x} -> 0x{event['to']:04x} "
            f"ret=0x{event['ret']:04x} depth={event['depth']}"
        )
    if event["type"] == "ret-underflow":
        return (
            f"{event['idx']:>8} RET-UNDERFLOW 0x{event['from']:04x} -> 0x{event['to']:04x}"
        )
    return f"{event['idx']:>8} FLOW {event}"


def read_header(fp):
    data = fp.read(HEADER_SIZE)
    if len(data) != HEADER_SIZE:
        raise ValueError("short header")
    magic, version, flags, range_start, range_end, init_size = struct.unpack(
        HEADER_FMT, data
    )
    if magic != MAGIC:
        raise ValueError("bad magic")
    init = fp.read(init_size)
    if len(init) != init_size:
        raise ValueError("short init snapshot")
    return {
        "version": version,
        "flags": flags,
        "range_start": range_start,
        "range_end": range_end,
        "init_size": init_size,
        "init": init,
    }


def iter_records(fp, resync=False):
    while True:
        typ = fp.read(1)
        if not typ:
            return
        rec_type = typ[0]
        if rec_type == 0x01:
            payload = fp.read(INSTR_SIZE)
            if len(payload) != INSTR_SIZE:
                raise ValueError("short instruction record")
            fields = struct.unpack(INSTR_FMT, payload)
            yield (rec_type, fields)
        elif rec_type == 0x02:
            payload = fp.read(MEM_WRITE_SIZE)
            if len(payload) != MEM_WRITE_SIZE:
                raise ValueError("short mem write record")
            addr, value = struct.unpack(MEM_WRITE_FMT, payload)
            yield (rec_type, (addr, value))
        elif rec_type == 0x03:
            payload = fp.read(KEY_EVENT_SIZE)
            if len(payload) != KEY_EVENT_SIZE:
                raise ValueError("short key event record")
            action, key, clock, pc = struct.unpack(KEY_EVENT_FMT, payload)
            yield (rec_type, (action, key, clock, pc))
        else:
            if resync:
                continue
            raise ValueError(f"unknown record type {rec_type}")


def format_instr(fields, labelmap=None):
    (
        pc,
        opcode,
        clock,
        af,
        bc,
        de,
        hl,
        ix,
        iy,
        sp,
        pc_reg,
        ir,
        wz,
        wz2,
        af2,
        bc2,
        de2,
        hl2,
        iff1,
        iff2,
        im,
        r7,
        halted,
    ) = fields
    label = ""
    if labelmap is not None:
        name, base = labelmap.lookup(pc)
        if name:
            label = f" {name}+0x{pc - base:x}"

    return (
        f"PC=0x{pc:04x} OP=0x{opcode:08x} CLK={clock} "
        f"AF=0x{af:04x} BC=0x{bc:04x} DE=0x{de:04x} HL=0x{hl:04x} "
        f"IX=0x{ix:04x} IY=0x{iy:04x} SP=0x{sp:04x} PC'=0x{pc_reg:04x} "
        f"IR=0x{ir:04x} WZ=0x{wz:04x} WZ2=0x{wz2:04x} "
        f"AF2=0x{af2:04x} BC2=0x{bc2:04x} DE2=0x{de2:04x} HL2=0x{hl2:04x} "
        f"IFF1={iff1} IFF2={iff2} IM={im} R7={r7} HALT={halted}{label}"
    )


class LabelMap:
    def __init__(self, labels):
        self.labels = sorted(labels, key=lambda x: x[0])
        self.by_name = {name: addr for addr, name in labels}

    @staticmethod
    def load(path):
        import json

        data = json.loads(open(path, "r").read())
        labels = [(entry["addr"], entry["name"]) for entry in data.get("labels", [])]
        return LabelMap(labels)

    def lookup(self, addr):
        lo = 0
        hi = len(self.labels) - 1
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            a, name = self.labels[mid]
            if a == addr:
                return (name, a)
            if a < addr:
                best = (name, a)
                lo = mid + 1
            else:
                hi = mid - 1
        return best if best else (None, None)

    def addr_for(self, name):
        return self.by_name.get(name)


def read_u16(mem_read, addr):
    lo = mem_read(addr)
    hi = mem_read(addr + 1)
    if lo is None or hi is None:
        return None
    return lo | (hi << 8)


def read_ram_u16(ram, hdr, addr):
    start = hdr["range_start"]
    end = hdr["range_end"]
    if addr < start or addr + 1 > end:
        return None
    off = addr - start
    return ram[off] | (ram[off + 1] << 8)


def infer_docol_target(words, mem_read):
    counts = {}
    for cfa in words:
        if mem_read(cfa) != 0xCD:
            continue
        target = read_u16(mem_read, cfa + 1)
        if target is None:
            continue
        counts[target] = counts.get(target, 0) + 1
    if not counts:
        return None
    return max(counts.items(), key=lambda x: x[1])[0]


def build_colon_words(words, mem_read, docol_target):
    colon = set()
    if docol_target is None:
        return colon
    for cfa, name in words.items():
        if mem_read(cfa) != 0xCD:
            continue
        target = read_u16(mem_read, cfa + 1)
        if target == docol_target:
            colon.add(cfa)
    return colon


def make_mem_reader(ram, hdr, rom_bytes=None):
    start = hdr["range_start"]
    end = hdr["range_end"]

    def read_u8(addr):
        if start <= addr <= end:
            return ram[addr - start]
        if rom_bytes is not None and 0 <= addr < len(rom_bytes):
            return rom_bytes[addr]
        return None

    return read_u8


def parse_forth_dictionary(latest_addr, mem_read, max_entries=20000):
    words = {}
    seen = set()
    addr = latest_addr
    while addr and addr not in seen and len(words) < max_entries:
        seen.add(addr)
        link = read_u16(mem_read, addr)
        if link is None:
            break
        len_flags = mem_read(addr + 2)
        if len_flags is None:
            break
        name_len = len_flags & 0x3F
        name_bytes = []
        for i in range(name_len):
            b = mem_read(addr + 3 + i)
            if b is None:
                break
            name_bytes.append(b)
        name = bytes(name_bytes).decode("ascii", "replace")
        cfa = addr + 3 + name_len + 1
        words[cfa] = name
        addr = link
    return words


def format_stack(ram, hdr, bc, sp, depth):
    start = hdr["range_start"]
    end = hdr["range_end"]
    items = [bc & 0xFFFF]
    addr = sp & 0xFFFF
    for _ in range(depth - 1):
        if addr < start or addr + 1 > end:
            items.append(None)
        else:
            off = addr - start
            val = ram[off] | (ram[off + 1] << 8)
            items.append(val)
        addr = (addr + 2) & 0xFFFF
    return items


def main():
    parser = argparse.ArgumentParser(description="Decode TilEm trace files")
    parser.add_argument("trace", help="Trace file")
    parser.add_argument("--print", dest="print_count", type=int, default=0,
                        help="Print first N instruction records")
    parser.add_argument("--step", action="store_true",
                        help="Interactively step through instructions")
    parser.add_argument("--dump-ram", metavar="FILE",
                        help="Write reconstructed RAM to FILE")
    parser.add_argument("--at", dest="at_index", type=int, default=0,
                        help="Instruction index for --dump-ram")
    parser.add_argument("--labelmap", metavar="FILE",
                        help="Label map JSON for PC symbol hints")
    parser.add_argument("--print-keys", action="store_true",
                        help="Print key press/release events")
    parser.add_argument("--stop-after-keys", type=int, default=0,
                        help="Stop after N key press events")
    parser.add_argument("--stop-on-sp-underflow", action="store_true",
                        help="Stop when SP drops below threshold")
    parser.add_argument("--sp-underflow-threshold", type=lambda x: int(x, 0),
                        default=0x8000, help="SP underflow threshold (default 0x8000)")
    parser.add_argument("--sp-underflow-window", type=int, default=8,
                        help="Instructions to show before underflow (default 8)")
    parser.add_argument("--print-flow", action="store_true",
                        help="Print call/ret/jump events")
    parser.add_argument("--print-untaken", action="store_true",
                        help="Include untaken conditional jumps/calls/returns")
    parser.add_argument("--flow-window", type=int, default=8,
                        help="Recent flow events to show on stop (default 8)")
    parser.add_argument("--stop-on-ret-underflow", action="store_true",
                        help="Stop when return occurs with empty call stack")
    parser.add_argument("--forth-trace", nargs="?", const="-", metavar="FILE",
                        help="Write Forth word call trace (default stdout)")
    parser.add_argument("--forth-rom", metavar="FILE",
                        help="ROM image for Forth dictionary lookup")
    parser.add_argument("--forth-latest", type=lambda x: int(x, 0),
                        help="Address of LATEST variable (overrides label map)")
    parser.add_argument("--forth-stack-depth", type=int, default=4,
                        help="Data stack depth to show on entry (default 4)")
    parser.add_argument("--forth-drop-underflow", action="store_true",
                        help="Stop on DROP when data stack is empty")
    parser.add_argument("--forth-sp0", type=lambda x: int(x, 0),
                        help="Address of SP0 variable (overrides label map)")
    parser.add_argument("--resync", action="store_true",
                        help="Skip unknown records (useful for ring backtraces)")

    args = parser.parse_args()

    labelmap = LabelMap.load(args.labelmap) if args.labelmap else None

    with open(args.trace, "rb") as fp:
        hdr = read_header(fp)
        print(
            f"version={hdr['version']} range=0x{hdr['range_start']:04x}-0x{hdr['range_end']:04x} "
            f"init_size={hdr['init_size']} flags=0x{hdr['flags']:04x}"
        )

        ram = None
        if args.dump_ram or args.forth_trace or args.forth_drop_underflow:
            ram = bytearray(hdr["init"])

        need_forth = args.forth_trace or args.forth_drop_underflow
        if need_forth:
            rom_bytes = None
            if args.forth_rom:
                rom_bytes = open(args.forth_rom, "rb").read()

            for rec_type, payload in iter_records(fp, resync=args.resync):
                if rec_type == 0x02:
                    addr, value = payload
                    if hdr["range_start"] <= addr <= hdr["range_end"]:
                        ram[addr - hdr["range_start"]] = value

            latest_addr = args.forth_latest
            if latest_addr is None and labelmap is not None:
                latest_addr = labelmap.addr_for("var-latest")

            if latest_addr is None:
                print("error: missing LATEST address; use --labelmap or --forth-latest",
                      file=sys.stderr)
                sys.exit(1)

            latest_val = read_u16(lambda a: ram[a - hdr["range_start"]]
                                  if hdr["range_start"] <= a <= hdr["range_end"]
                                  else None,
                                  latest_addr)
            if latest_val is None or latest_val == 0:
                print("error: LATEST value is missing or zero", file=sys.stderr)
                sys.exit(1)

            mem_read = make_mem_reader(ram, hdr, rom_bytes)
            words = parse_forth_dictionary(latest_val, mem_read)
            if not words:
                print("error: no Forth words found from dictionary", file=sys.stderr)
                sys.exit(1)

            name_to_addr = {}
            for addr, name in words.items():
                name_to_addr.setdefault(name, addr)
            drop_addr = name_to_addr.get("DROP")
            exit_addr = name_to_addr.get("EXIT")
            sp0_addr = args.forth_sp0
            if sp0_addr is None and labelmap is not None:
                sp0_addr = labelmap.addr_for("var-sp0")
            if args.forth_drop_underflow and sp0_addr is None:
                print("error: missing SP0 address; use --labelmap or --forth-sp0",
                      file=sys.stderr)
                sys.exit(1)

            docol_target = infer_docol_target(words, mem_read)
            colon_words = build_colon_words(words, mem_read, docol_target)

            if args.forth_trace == "-":
                out = sys.stdout
            elif args.forth_trace:
                out = open(args.forth_trace, "w")
            else:
                out = None

            try:
                fp.seek(0)
                hdr = read_header(fp)
                ram = bytearray(hdr["init"])
                instr_index = 0
                forth_stack = []
                last_word = None
                prev_sp = None
                for rec_type, payload in iter_records(fp, resync=args.resync):
                    if rec_type == 0x02:
                        addr, value = payload
                        if hdr["range_start"] <= addr <= hdr["range_end"]:
                            ram[addr - hdr["range_start"]] = value
                        continue
                    if rec_type != 0x01:
                        continue

                    pc = payload[IDX_PC]
                    if pc in words:
                        last_word = words[pc]
                        if args.forth_drop_underflow and pc in colon_words:
                            forth_stack.append(words[pc])
                        if args.forth_drop_underflow and exit_addr == pc:
                            if forth_stack:
                                forth_stack.pop()
                    if args.forth_trace and pc in words:
                        bc = payload[IDX_BC]
                        sp = payload[IDX_SP]
                        stack = format_stack(ram, hdr, bc, sp,
                                             max(1, args.forth_stack_depth))
                        stack_str = " ".join(
                            "??" if v is None else f"0x{v:04x}"
                            for v in stack
                        )
                        out.write(
                            f"{instr_index:>8} PC=0x{pc:04x} {words[pc]} "
                            f"BC=0x{bc:04x} SP=0x{sp:04x} STACK=[{stack_str}]\n"
                        )

                    if args.forth_drop_underflow and drop_addr == pc:
                        sp0_val = None
                        if sp0_addr is not None:
                            sp0_val = read_ram_u16(ram, hdr, sp0_addr)
                        if sp0_val is not None and prev_sp is not None:
                            if prev_sp >= sp0_val:
                                caller = forth_stack[-1] if forth_stack else None
                                state = "underflow" if prev_sp > sp0_val else "empty"
                                print(f"DROP {state} stack detected:")
                                print(
                                    f"instr={instr_index} PC=0x{pc:04x} "
                                    f"SP(before)=0x{prev_sp:04x} SP(after)=0x{payload[IDX_SP]:04x} "
                                    f"SP0=0x{sp0_val:04x}"
                                )
                                if caller:
                                    print(f"caller={caller}")
                                if forth_stack:
                                    print("forth-stack=" + " -> ".join(forth_stack))
                                if last_word:
                                    print(f"last-word={last_word}")
                                print(format_instr(payload, labelmap))
                                return

                    prev_sp = payload[IDX_SP]
                    instr_index += 1
            finally:
                if out is not None and out is not sys.stdout:
                    out.close()
            return

        instr_count = 0
        printed = 0
        key_presses = 0
        underflow_window = []
        flow_window = []
        call_stack = []
        prev_pc_reg = None
        prev_sp = None

        def record_flow(event):
            if args.print_flow:
                print(format_flow(event))
            flow_window.append(event)
            if len(flow_window) > args.flow_window:
                flow_window.pop(0)

        def dump_flow_context():
            if not flow_window:
                return
            print("Recent control flow:")
            for event in flow_window:
                print(format_flow(event))
        for rec_type, payload in iter_records(fp, resync=args.resync):
            if rec_type == 0x02 and ram is not None:
                addr, value = payload
                if hdr["range_start"] <= addr <= hdr["range_end"]:
                    ram[addr - hdr["range_start"]] = value
                continue
            if rec_type == 0x03:
                action, key, clock, pc = payload
                if args.print_keys:
                    state = "down" if action else "up"
                    print(f"{instr_count:>8} KEY {state} code={key} PC=0x{pc:04x} CLK={clock}")
                if action:
                    key_presses += 1
                    if args.stop_after_keys and key_presses >= args.stop_after_keys:
                        print(f"stop-after-keys reached at {key_presses} presses (instr {instr_count})")
                        break
                continue

            if rec_type != 0x01:
                continue

            pc = payload[IDX_PC]
            opcode = payload[IDX_OPCODE]
            sp = payload[IDX_SP]
            pc_reg = payload[IDX_PC_REG]

            if prev_pc_reg is not None and pc != prev_pc_reg:
                if prev_sp is not None and ((prev_sp - 2) & 0xFFFF) == sp:
                    event = {
                        "type": "async",
                        "idx": instr_count,
                        "from": prev_pc_reg,
                        "to": pc,
                        "ret": prev_pc_reg,
                        "depth": len(call_stack) + 1,
                    }
                    call_stack.append(prev_pc_reg)
                    record_flow(event)

            flow = control_flow_info(opcode)
            if flow:
                seq_pc = (pc + flow["len"]) & 0xFFFF
                taken = (pc_reg != seq_pc)
                is_cond = flow["conditional"]
                if not taken and is_cond and args.print_untaken:
                    record_flow({
                        "type": "jump",
                        "idx": instr_count,
                        "from": pc,
                        "to": pc_reg,
                        "kind": flow["kind"],
                        "taken": False,
                    })
                if taken or not is_cond:
                    if flow["kind"] in {"call", "rst"}:
                        event = {
                            "type": "call",
                            "idx": instr_count,
                            "from": pc,
                            "to": pc_reg,
                            "ret": seq_pc,
                            "depth": len(call_stack) + 1,
                        }
                        call_stack.append(seq_pc)
                        record_flow(event)
                    elif flow["kind"] == "ret":
                        if call_stack:
                            expected = call_stack.pop()
                            event = {
                                "type": "ret",
                                "idx": instr_count,
                                "from": pc,
                                "to": pc_reg,
                                "ret": expected,
                                "depth": len(call_stack),
                                "mismatch": expected != pc_reg,
                            }
                            record_flow(event)
                        else:
                            event = {
                                "type": "ret-underflow",
                                "idx": instr_count,
                                "from": pc,
                                "to": pc_reg,
                            }
                            record_flow(event)
                            if args.stop_on_ret_underflow:
                                print("Return underflow detected:")
                                print(format_flow(event))
                                dump_flow_context()
                                break
                    else:
                        if taken or args.print_untaken:
                            record_flow({
                                "type": "jump",
                                "idx": instr_count,
                                "from": pc,
                                "to": pc_reg,
                                "kind": flow["kind"],
                                "taken": taken,
                            })

            if args.print_count and printed < args.print_count:
                print(format_instr(payload, labelmap))
                printed += 1
                if printed >= args.print_count and not args.step and not args.dump_ram:
                    break

            if args.step:
                print(format_instr(payload, labelmap))
                try:
                    line = input("step> ")
                except EOFError:
                    line = "q"
                if line.strip().lower() in {"q", "quit", "exit"}:
                    break

            if args.stop_on_sp_underflow:
                underflow_window.append((instr_count, payload))
                if len(underflow_window) > (args.sp_underflow_window * 2 + 1):
                    underflow_window.pop(0)
                if sp < args.sp_underflow_threshold:
                    print("SP underflow detected:")
                    for idx, fields in underflow_window:
                        print(f"{idx:>8} {format_instr(fields, labelmap)}")
                    dump_flow_context()
                    break
            else:
                if underflow_window:
                    underflow_window.clear()

            prev_pc_reg = pc_reg
            prev_sp = sp

            if args.dump_ram and instr_count == args.at_index:
                with open(args.dump_ram, "wb") as out:
                    out.write(ram)
                print(f"wrote RAM dump at instruction {instr_count} -> {args.dump_ram}")
                return

            instr_count += 1

        if args.dump_ram:
            with open(args.dump_ram, "wb") as out:
                out.write(ram)
            print(f"wrote RAM dump at end -> {args.dump_ram}")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
