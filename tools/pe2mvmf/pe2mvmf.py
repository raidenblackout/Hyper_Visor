
"""pe2mvmf -- convert a PE image to the MVM flat (MVMF) binary format.

The MVM hypervisor builds as a plain PE (mvm-hv.pe) because that is what
MSVC's linker emits; nothing in the boot chain reads the PE headers.
This tool strips the PE overhead and emits a compact flat binary whose
sole reader is bootloader/include/flat_loader.h.

MVMF layout on disk:

    [ mvm_flat_header ]     32 bytes, packed
    [ image bytes    ]      image_size bytes -- exactly what a PE loader
                            would produce after unpacking sections into
                            their virtual layout (zero-filled bss included)
    [ reloc list     ]      reloc_count * uint32_t, each an RVA within the
                            image where a 64-bit absolute address needs a
                            (actual_base - link_base) delta added at load
                            time. Only DIR64 relocs are emitted; ABSOLUTE
                            padding entries are dropped. Any other reloc
                            type in the PE is a hard error.

The entry point is looked up in the PE export directory by name (default:
hvruntime_boot_entry). The PE's own AddressOfEntryPoint is ignored -- the
flat blob has no notion of a "PE entry point" at all.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

MVMF_MAGIC = 0x464D564D
MVMF_VERSION = 1
MVMF_HEADER_SIZE = 40

def pack_mvmf_header(image_size: int, entry_offset: int, link_base: int,
                     reloc_count: int, reloc_off: int) -> bytes:

    hdr = struct.pack("<IIIIIIIIQ",
                      MVMF_MAGIC,
                      MVMF_VERSION,
                      MVMF_HEADER_SIZE,
                      image_size,
                      entry_offset,
                      reloc_count,
                      reloc_off,
                      0,
                      link_base)
    assert len(hdr) == MVMF_HEADER_SIZE, f"header packed to {len(hdr)}, expected {MVMF_HEADER_SIZE}"
    return hdr

IMAGE_DOS_SIGNATURE = 0x5A4D
IMAGE_NT_SIGNATURE  = 0x00004550
IMAGE_NT_OPTIONAL_HDR64_MAGIC = 0x20B

IMAGE_DIRECTORY_ENTRY_EXPORT   = 0
IMAGE_DIRECTORY_ENTRY_BASERELOC = 5

IMAGE_REL_BASED_ABSOLUTE = 0
IMAGE_REL_BASED_DIR64    = 10

class PeError(RuntimeError):
    pass

def _u16(b, o): return struct.unpack_from("<H", b, o)[0]
def _u32(b, o): return struct.unpack_from("<I", b, o)[0]
def _u64(b, o): return struct.unpack_from("<Q", b, o)[0]

def parse_pe(raw: bytes):
    if len(raw) < 64 or _u16(raw, 0) != IMAGE_DOS_SIGNATURE:
        raise PeError("not a PE (bad DOS signature)")
    e_lfanew = _u32(raw, 0x3C)
    if e_lfanew + 24 > len(raw) or _u32(raw, e_lfanew) != IMAGE_NT_SIGNATURE:
        raise PeError("not a PE (bad NT signature)")

    fh_off = e_lfanew + 4
    num_sections = _u16(raw, fh_off + 2)
    size_of_opt  = _u16(raw, fh_off + 16)
    oh_off = fh_off + 20

    if _u16(raw, oh_off) != IMAGE_NT_OPTIONAL_HDR64_MAGIC:
        raise PeError("not PE32+ (only 64-bit PE is supported)")

    size_of_image   = _u32(raw, oh_off + 56)
    size_of_headers = _u32(raw, oh_off + 60)
    image_base      = _u64(raw, oh_off + 24)
    num_dirs        = _u32(raw, oh_off + 108)

    if num_dirs < 6:
        raise PeError("PE optional header missing baserel directory entry")

    def data_dir(i):
        base = oh_off + 112 + i * 8
        return _u32(raw, base), _u32(raw, base + 4)

    export_rva, export_size = data_dir(IMAGE_DIRECTORY_ENTRY_EXPORT)
    reloc_rva,  reloc_size  = data_dir(IMAGE_DIRECTORY_ENTRY_BASERELOC)

    sec_off = oh_off + size_of_opt
    sections = []
    for i in range(num_sections):
        s = sec_off + i * 40
        sections.append({
            "name":        raw[s : s + 8].rstrip(b"\x00").decode("latin-1", "replace"),
            "vsize":       _u32(raw, s + 8),
            "vaddr":       _u32(raw, s + 12),
            "raw_size":    _u32(raw, s + 16),
            "raw_ptr":     _u32(raw, s + 20),
        })

    return {
        "image_base":      image_base,
        "size_of_image":   size_of_image,
        "size_of_headers": size_of_headers,
        "sections":        sections,
        "export":          (export_rva, export_size),
        "reloc":           (reloc_rva,  reloc_size),
    }

def unpack_image(raw: bytes, pe) -> bytearray:
    """Emit exactly what a runtime PE loader would produce -- headers copied
    into the first size_of_headers bytes, each section blitted at its RVA,
    everything else zero (implicit bss)."""
    img = bytearray(pe["size_of_image"])
    img[: pe["size_of_headers"]] = raw[: pe["size_of_headers"]]
    for s in pe["sections"]:
        vaddr = s["vaddr"]
        vsize = s["vsize"]
        rsize = s["raw_size"]
        rptr  = s["raw_ptr"]
        to_copy = min(rsize, vsize)
        if to_copy:
            if rptr + to_copy > len(raw):
                raise PeError(f"section {s['name']} raw range past EOF")
            img[vaddr : vaddr + to_copy] = raw[rptr : rptr + to_copy]
    return img

def collect_dir64_relocs(pe) -> list[int]:
    """Walk the BASERELOC directory, return sorted list of RVAs where a
    64-bit absolute needs (actual_base - link_base) added at load time.
    Rejects any reloc type other than ABSOLUTE (padding) or DIR64 -- MSVC/x64
    for this codebase only emits those two, and any other type would silently
    break relocation."""
    reloc_rva, reloc_size = pe["reloc"]
    if reloc_size == 0 or reloc_rva == 0:
        return []

    out = []

    img = pe["_image"]
    end = reloc_rva + reloc_size
    p = reloc_rva
    while p + 8 <= end:
        block_va   = _u32(img, p)
        block_size = _u32(img, p + 4)
        if block_size < 8 or p + block_size > end:
            raise PeError(f"malformed reloc block at 0x{p:x} (size=0x{block_size:x})")
        entries = (block_size - 8) // 2
        for i in range(entries):
            e = _u16(img, p + 8 + i * 2)
            t = e >> 12
            o = e & 0x0FFF
            if t == IMAGE_REL_BASED_ABSOLUTE:
                continue
            if t != IMAGE_REL_BASED_DIR64:
                raise PeError(f"unexpected reloc type {t} at block va=0x{block_va:x}, offset=0x{o:x}")
            out.append(block_va + o)
        p += block_size
    out.sort()
    return out

def find_export_rva(pe, name: str) -> int:
    export_rva, export_size = pe["export"]
    if export_rva == 0 or export_size == 0:
        raise PeError("PE has no export table")

    img = pe["_image"]

    num_funcs   = _u32(img, export_rva + 20)
    num_names   = _u32(img, export_rva + 24)
    addr_funcs  = _u32(img, export_rva + 28)
    addr_names  = _u32(img, export_rva + 32)
    addr_ords   = _u32(img, export_rva + 36)

    for i in range(num_names):
        name_rva = _u32(img, addr_names + i * 4)

        j = name_rva
        while j < len(img) and img[j] != 0:
            j += 1
        candidate = img[name_rva:j].decode("latin-1", "replace")
        if candidate == name:
            ord_idx = _u16(img, addr_ords + i * 2)
            if ord_idx >= num_funcs:
                raise PeError(f"export {name} has bad ordinal {ord_idx}")
            fn_rva = _u32(img, addr_funcs + ord_idx * 4)
            if fn_rva == 0:
                raise PeError(f"export {name} is forwarded / zero")
            return fn_rva
    raise PeError(f"export '{name}' not found")

def convert(pe_path: Path, out_path: Path, entry_name: str) -> None:
    raw = pe_path.read_bytes()
    pe = parse_pe(raw)
    pe["_image"] = unpack_image(raw, pe)
    entry_rva = find_export_rva(pe, entry_name)
    relocs = collect_dir64_relocs(pe)

    image_bytes = bytes(pe["_image"])
    image_size = len(image_bytes)
    reloc_off = MVMF_HEADER_SIZE + image_size

    header = pack_mvmf_header(
        image_size=image_size,
        entry_offset=entry_rva,
        link_base=pe["image_base"],
        reloc_count=len(relocs),
        reloc_off=reloc_off,
    )

    reloc_blob = struct.pack(f"<{len(relocs)}I", *relocs)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(header)
        f.write(image_bytes)
        f.write(reloc_blob)

    print(f"pe2mvmf: {pe_path.name} -> {out_path.name}")
    print(f"  image_size    = {image_size} ({image_size/1024:.1f} KiB)")
    print(f"  entry_rva     = 0x{entry_rva:X}  ({entry_name})")
    print(f"  link_base     = 0x{pe['image_base']:X}")
    print(f"  reloc_count   = {len(relocs)}")
    print(f"  output_size   = {out_path.stat().st_size} bytes")

def main(argv=None):
    ap = argparse.ArgumentParser(description="Convert a PE image to MVMF flat.")
    ap.add_argument("pe", type=Path, help="input PE (.sys) file")
    ap.add_argument("out", type=Path, help="output .bin path")
    ap.add_argument("--entry", default="hvruntime_boot_entry",
                    help="exported symbol name used as the flat entry (default: %(default)s)")
    args = ap.parse_args(argv)

    try:
        convert(args.pe, args.out, args.entry)
    except (PeError, OSError) as e:
        print(f"pe2mvmf: ERROR: {e}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
