"""Helpers for Fire Emblem: Path of Radiance disc files (LZ10 .cmp and 'pack' archives)."""
import struct

def lz10(data):
    assert data[0] == 0x10, 'not LZ10'
    size = data[1] | data[2] << 8 | data[3] << 16
    out = bytearray(); i = 4
    while len(out) < size:
        flags = data[i]; i += 1
        for bit in range(8):
            if len(out) >= size: break
            if flags & (0x80 >> bit):
                b1, b2 = data[i], data[i+1]; i += 2
                length = (b1 >> 4) + 3
                disp = ((b1 & 0xF) << 8 | b2) + 1
                for _ in range(length):
                    out.append(out[-disp])
            else:
                out.append(data[i]); i += 1
    return bytes(out)

def pack(data):
    """Returns {name: bytes} for a 'pack' archive."""
    assert data[:4] == b'pack', data[:4]
    count = struct.unpack_from('>H', data, 4)[0]
    files = {}
    for k in range(count):
        _, name_off, data_off, size = struct.unpack_from(">IIII", data, 8 + k * 16)
        e = data.index(b'\0', name_off)
        files[data[name_off:e].decode('latin1')] = data[data_off:data_off + size]
    return files
