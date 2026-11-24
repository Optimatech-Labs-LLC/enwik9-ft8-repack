# SPDX-License-Identifier: GPL-3.0-only
"""Preserve the original NRV2B copy decisions in the ft8 executable."""
import io
import lzma
import struct

PROLOGUE = 468
BLOCKS = 5
BLOCK = struct.Struct("<IIBBBB")
PAIR = struct.Struct("<II")
LENGTHS = struct.Struct("<5I")
SCAFFOLD = struct.Struct("<I")
END_OFFSET = 0xffffffff


class BitReader:
    def __init__(self, data):
        self.data = io.BytesIO(data)
        self.word = 0
        self.left = 0

    def take(self, size):
        field = self.data.read(size)
        if len(field) != size:
            raise ValueError("truncated NRV block")
        return field

    def bit(self):
        if not self.left:
            self.word = int.from_bytes(self.take(4), "little")
            self.left = 32
        self.left -= 1
        return (self.word >> self.left) & 1

    def integer(self):
        value = 1
        for _ in range(25):
            value = value * 2 + self.bit()
            if self.bit():
                return value
        raise ValueError("oversize NRV integer")


class BitWriter:
    def __init__(self):
        self.data = bytearray()
        self.left = 0
        self.head = 0

    def bit(self, value):
        if not self.left:
            self.head = len(self.data)
            self.data.extend(bytes(4))
            self.left = 32
        self.left -= 1
        self.data[self.head + self.left // 8] |= (value & 1) << (self.left % 8)

    def integer(self, value):
        for shift in reversed(range(value.bit_length() - 1)):
            self.bit(value >> shift)
            self.bit(shift == 0)


def record_block(stream, expanded_size):
    reader = BitReader(stream)
    records = bytearray()
    position = 0
    last_distance = 1
    while True:
        if reader.bit():
            literal = reader.take(1)[0]
            records.extend(PAIR.pack(0, literal))
            position += 1
        else:
            code = reader.integer()
            if code != 2:
                offset = (code - 3) * 256 + reader.take(1)[0]
                if offset == END_OFFSET:
                    break
                last_distance = offset + 1
            short = 2 * reader.bit() + reader.bit()
            length = (short or reader.integer() + 2) + 1 + (last_distance > 0xd00)
            if last_distance > position:
                raise ValueError("invalid NRV copy distance")
            records.extend(PAIR.pack(length, 0 if code == 2 else last_distance))
            position += length
        if position > expanded_size:
            raise ValueError("NRV block exceeds declared length")
    if position != expanded_size or reader.data.read(1) or reader.word & ((1 << reader.left) - 1):
        raise ValueError("noncanonical NRV ending")
    return records


def write_block(events, size):
    writer = BitWriter()
    position = 0
    last_distance = 1
    while position < size:
        record = events.read(PAIR.size)
        if len(record) != PAIR.size:
            raise ValueError("missing NRV record")
        length, value = PAIR.unpack(record)
        writer.bit(length == 0)
        if length == 0:
            writer.data.append(value)
            position += 1
            continue
        if value:
            last_distance = value
            writer.integer((value - 1) // 256 + 3)
            writer.data.append((value - 1) % 256)
        else:
            writer.integer(2)
        short = length - 1 - (last_distance > 0xd00)
        if not 0 < last_distance <= position or short < 1 or position + length > size:
            raise ValueError("invalid decoded NRV record")
        if short <= 3:
            writer.bit(short >> 1)
            writer.bit(short)
        else:
            writer.bit(0)
            writer.bit(0)
            writer.integer(short - 2)
        position += length
    writer.bit(0)
    writer.integer(END_OFFSET // 256 + 3)
    writer.data.append(255)
    return writer.data


def block_sizes(header):
    size, compressed, method, _, _, reserved = BLOCK.unpack(header)
    if method != 2 or reserved or not 0 < compressed < size <= 1_000_000:
        raise ValueError("unexpected ft8 executable block")
    return size, compressed


def pack_prefix(data, transcode):
    position = PROLOGUE
    scaffold = bytearray(data[:position])
    sizes = []
    records = bytearray()
    for _ in range(BLOCKS):
        header = data[position:position + BLOCK.size]
        size, compressed = block_sizes(header)
        scaffold.extend(header)
        position += BLOCK.size
        sizes.append(size)
        records.extend(record_block(data[position:position + compressed], size))
        position += compressed
    scaffold.extend(data[position:])
    filters = [dict(id=lzma.FILTER_LZMA1, dict_size=1 << 20, lc=3, lp=0, pb=0, nice_len=273)]
    compact = lzma.compress(scaffold, format=lzma.FORMAT_ALONE, filters=filters)
    return SCAFFOLD.pack(len(compact)) + compact + transcode("encode-prefix", LENGTHS.pack(*sizes) + records)


def unpack_prefix(data, transcode):
    compact_size, = SCAFFOLD.unpack_from(data)
    stop = SCAFFOLD.size + compact_size
    if stop >= len(data):
        raise ValueError("truncated executable prefix")
    scaffold = io.BytesIO(lzma.decompress(data[SCAFFOLD.size:stop], format=lzma.FORMAT_ALONE, memlimit=32 << 20))
    events = io.BytesIO(transcode("decode-prefix", data[stop:]))
    sizes = LENGTHS.unpack(events.read(LENGTHS.size))
    result = bytearray(scaffold.read(PROLOGUE))
    for expected in sizes:
        header = scaffold.read(BLOCK.size)
        size, compressed = block_sizes(header)
        if size != expected:
            raise ValueError("block dimensions disagree")
        binary = write_block(events, size)
        if len(binary) != compressed:
            raise ValueError("reconstructed NRV size mismatch")
        result.extend(header)
        result.extend(binary)
    if events.read(1):
        raise ValueError("unused NRV records")
    result.extend(scaffold.read())
    return result
