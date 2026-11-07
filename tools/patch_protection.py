#!/usr/bin/env python3
"""patch_protection.py IN.EXE OUT.EXE - disable Fox Ranger's key-file check.

The check (unpacked FOX.EXE, load-image offsets; file offset = img + 0x25E0):

  img 05EF0  04A4:14B0  check_keyfiles()  far proc
      _dos_findfirst("\\EV22.SYS" | "EV22.SYS", _A_RDONLY|_A_HIDDEN) size == 5100
      _dos_findfirst("\\EV21.SYS" | "EV21.SYS", ...)                   size == 512
      returns AL=1 on failure (file contents are never read)
  img 04A57  lcall 04A4:14B0
  img 04A62  75 03   jne 4A67      <- failure branch
  img 04A64  E9 1C00 jmp 4A83      (continue start-up)
  img 04A67  puts("\\aUnauthorized user !!!. Please don't copy.."); exit(1)

Patches (both verified against the original bytes before writing):
  1. img 04A62: 75 03 -> EB 1F   jmp 4A83  (never take the error path)
  2. img 05EF0: 55 8B EC -> 30 C0 CB   xor al,al / retf  (checker always
     passes, so EV21.SYS/EV22.SYS are no longer needed at all)
"""
import sys

HDR = 0x25E0  # MZ header size of the rebuilt FOX_UNP2.EXE
PATCHES = [
    (0x04A62, bytes.fromhex('7503'), bytes.fromhex('EB1F'), 'error branch -> jmp continue'),
    (0x05EF0, bytes.fromhex('558BEC'), bytes.fromhex('30C0CB'), 'check_keyfiles -> return 0'),
]


def apply(data):
    data = bytearray(data)
    if data[:2] != b'MZ' or int.from_bytes(data[8:10], 'little') * 16 != HDR:
        raise SystemExit('not the unpacked FOX_UNP2.EXE (header size mismatch)')
    for img, old, new, what in PATCHES:
        off = HDR + img
        cur = bytes(data[off:off + len(old)])
        if cur == new:
            print(f'  img {img:05X}: already patched ({what})')
            continue
        if cur != old:
            raise SystemExit(f'img {img:05X}: expected {old.hex()} found {cur.hex()} - wrong binary?')
        data[off:off + len(new)] = new
        print(f'  img {img:05X} (file {off:#07x}): {old.hex()} -> {new.hex()}  {what}')
    return bytes(data)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    out = apply(open(sys.argv[1], 'rb').read())
    open(sys.argv[2], 'wb').write(out)
    print('wrote', sys.argv[2])
