// SPDX-License-Identifier: GPL-3.0-only
// Lossless transcoder for the FX2TFWC2 stream described by fx2-cmix-transformer
// and ft8. The alternate model codes the fifteen legal quantizer symbols using
// tensor-local counts and optional column or magnitude contexts.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;
using Tree = std::array<uint16_t, 256>;
constexpr uint32_t top = 1u << 24;

void require(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}

uint32_t u32(const Bytes& bytes, size_t pos) {
    require(pos + 4 <= bytes.size(), "truncated integer");
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= uint32_t(bytes[pos+i]) << (8*i);
    return value;
}

struct RangeIn {
    const Bytes& data;
    size_t pos;
    uint32_t range = UINT32_MAX, code = 0;
    uint8_t get() {
        require(pos < data.size(), "truncated range stream");
        return data[pos++];
    }
    RangeIn(const Bytes& bytes, size_t start) : data(bytes), pos(start) {
        require(get() == 0, "unexpected initial range byte");
        for (int i = 0; i < 4; ++i) code = (code << 8) | get();
    }
    unsigned read(uint16_t prob) {
        uint32_t split = (range >> 11) * prob;
        unsigned bit = code >= split;
        if (bit) { code -= split; range -= split; }
        else range = split;
        while (range < top) { range <<= 8; code = (code << 8) | get(); }
        return bit;
    }
};

struct RangeOut {
    Bytes out;
    uint64_t low = 0;
    uint32_t range = UINT32_MAX, pending = 1;
    uint8_t cache = 0;
    void shift() {
        uint32_t bottom = uint32_t(low), carry = uint32_t(low >> 32);
        if (bottom < 0xff000000u || carry) {
            out.push_back(uint8_t(cache + carry));
            for (uint32_t i = 1; i < pending; ++i) out.push_back(uint8_t(255 + carry));
            cache = uint8_t(bottom >> 24);
            pending = 0;
        }
        ++pending;
        low = bottom << 8;
    }
    void put(uint16_t prob, unsigned bit) {
        uint32_t split = (range >> 11) * prob;
        if (bit) { low += split; range -= split; }
        else range = split;
        while (range < top) { range <<= 8; shift(); }
    }
    void finish() { for (int i = 0; i < 5; ++i) shift(); }
};

struct Counts {
    uint32_t zero = 0, total = 0;
    void add(unsigned bit) {
        zero += bit == 0;
        ++total;
        if (total == 32768) { zero = (zero + 1) / 2; total /= 2; }
    }
    uint16_t with_prior(uint16_t prob, unsigned strength) const {
        return uint16_t(std::clamp((zero * 2048 + strength * prob) / (total + strength), 1u, 2047u));
    }
};

using QuantTree = std::array<Counts,16>;
struct Quant {
    QuantTree global{};
    std::vector<QuantTree> local;
    unsigned width = 1, mode = 0, last = 7;
    void reset(unsigned columns, unsigned recipe) {
        width = std::max(columns,1u); mode = recipe; last = 7; global = {};
        unsigned bins = 1;
        if (mode == 1 || mode == 4) bins = std::min(width,4096u);
        else if (mode == 2) bins = 32;
        else if (mode == 3) bins = 8;
        if (mode == 4) bins *= 4;
        local.assign(bins, {});
    }
    unsigned bin(size_t index) const {
        if (mode == 1) return (index % width) % local.size();
        if (mode == 2) return index % 32;
        unsigned magnitude = unsigned(std::abs(int(last)-7));
        if (mode == 3) return magnitude;
        if (mode == 4) return ((index % width) % (local.size()/4)) * 4 + magnitude/2;
        return 0;
    }
};

struct Transcode {
    RangeIn from;
    RangeOut to;
    bool packing;
    unsigned mode;
    std::map<unsigned,Tree> legacy;
    Quant quant;
    Transcode(const Bytes& bytes, bool pack, unsigned m) :
        from(bytes, pack ? 12 : 13), packing(pack), mode(m) {}
    Tree& tree(unsigned key) {
        auto [it,inserted] = legacy.try_emplace(key);
        if (inserted) it->second.fill(1024);
        return it->second;
    }
    unsigned symbol(unsigned key, unsigned width, size_t pos = 0) {
        Tree& old = tree(key);
        bool weight = key == 100001;
        unsigned bin = weight ? quant.bin(pos) : 0;
        unsigned node = 1;
        for (unsigned level = 0; level < width; ++level) {
            uint16_t original = old[node], alternate = original;
            if (weight) {
                alternate = quant.global[node].with_prior(1024,2);
                if (mode) alternate = quant.local[bin][node].with_prior(alternate,64);
            }
            bool implied = weight && node == 15;
            unsigned bit;
            if (packing) {
                bit = from.read(original);
                if (implied) require(bit == 0, "unused int4 code");
                else to.put(alternate,bit);
            } else {
                bit = implied ? 0 : from.read(alternate);
                to.put(original,bit);
            }
            old[node] = bit ? original - (original >> 5) : original + ((2048-original) >> 5);
            if (weight) {
                quant.global[node].add(bit);
                if (mode) quant.local[bin][node].add(bit);
            }
            node = 2*node+bit;
        }
        unsigned value = node - (1u << width);
        if (weight) quant.last = value;
        return value;
    }
    void tensor_stream(uint32_t tensors) {
        unsigned previous = 0;
        auto meta = [&] { previous = symbol(previous,8); return previous; };
        for (uint32_t t = 0; t < tensors; ++t) {
            unsigned name_size = meta(), suffix = 0;
            for (unsigned i = 0; i < name_size; ++i) suffix = ((suffix << 8) | symbol(256 + suffix,8)) & 65535;
            unsigned type = meta(), dims = meta();
            require(type <= 3 && dims <= 12, "invalid tensor shape");
            uint64_t count = 1;
            uint32_t columns = 1;
            for (unsigned i = 0; i < dims; ++i) {
                columns = 0;
                for (unsigned j = 0; j < 4; ++j) columns |= uint32_t(meta()) << (8*j);
                require(columns <= (1u<<27) && count <= (1u<<27)/std::max(1u,columns), "oversize tensor");
                count *= columns;
            }
            unsigned encoding = meta();
            if (encoding == 1) {
                quant.reset(columns, mode);
                for (size_t i=0; i<count; ++i) symbol(100001,4,i);
            } else if (encoding == 2) {
                for (size_t i=0; i<count; ++i) { unsigned hi = symbol(100002,8); symbol(101000+hi,8); }
            } else if (encoding == 3) {
                for (size_t i=0; i<count*4; ++i) symbol(102000+(i%4),8);
            } else if (encoding == 0) {
                unsigned sizes[] = {1,2,4,4};
                for (size_t i=0; i<count*sizes[type]; ++i) symbol(100000,8);
            } else require(encoding == 4 || encoding == 5, "unknown tensor encoding");
        }
        to.finish();
    }
};

int main(int argc, char** argv) try {
    require(argc==4 || argc==5, "usage: weight15 pack|unpack INPUT OUTPUT [0-4]");
    std::string action = argv[1];
    bool pack = action == "pack";
    require(pack || action == "unpack", "unknown command");
    std::ifstream input(argv[2], std::ios::binary);
    require(bool(input), "cannot open input");
    Bytes bytes((std::istreambuf_iterator<char>(input)), {});
    std::string old_magic="FX2TFWC2", new_magic="W15CTX01";
    require(bytes.size() >= 18 && std::equal(bytes.begin(),bytes.begin()+8,(pack ? old_magic : new_magic).begin()),"invalid header");
    uint32_t n = u32(bytes,8);
    require(n && n<=65536, "invalid tensor count");
    unsigned m = pack ? (argc == 5 ? std::stoul(argv[4]) : 0) : bytes[12];
    require(m<=4,"invalid model");
    Transcode coder(bytes,pack,m);
    coder.tensor_stream(n);
    std::ofstream out(argv[3], std::ios::binary);
    const auto& magic = pack ? new_magic : old_magic;
    out.write(magic.data(),8);
    out.write(reinterpret_cast<const char*>(bytes.data()+8),4);
    if (pack) out.put(char(m));
    out.write(reinterpret_cast<const char*>(coder.to.out.data()),coder.to.out.size());
    out.close();
    require(bool(out), "output write failed");
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
