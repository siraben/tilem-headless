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


def iter_records(fp):
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

    args = parser.parse_args()

    labelmap = LabelMap.load(args.labelmap) if args.labelmap else None

    with open(args.trace, "rb") as fp:
        hdr = read_header(fp)
        print(
            f"version={hdr['version']} range=0x{hdr['range_start']:04x}-0x{hdr['range_end']:04x} "
            f"init_size={hdr['init_size']} flags=0x{hdr['flags']:04x}"
        )

        ram = None
        if args.dump_ram:
            ram = bytearray(hdr["init"])

        instr_count = 0
        printed = 0
        key_presses = 0
        underflow_window = []
        for rec_type, payload in iter_records(fp):
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
                sp = payload[3 + 6]
                underflow_window.append((instr_count, payload))
                if len(underflow_window) > (args.sp_underflow_window * 2 + 1):
                    underflow_window.pop(0)
                if sp < args.sp_underflow_threshold:
                    print("SP underflow detected:")
                    for idx, fields in underflow_window:
                        print(f"{idx:>8} {format_instr(fields, labelmap)}")
                    break
            else:
                if underflow_window:
                    underflow_window.clear()

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
