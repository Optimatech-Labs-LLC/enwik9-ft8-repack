// SPDX-License-Identifier: GPL-3.0-only
// Reversible magnitude-plane recoding for the ft8 transformer.
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

struct Transcode {
    RangeIn input;
    RangeOut output;
    bool packing;
    std::map<unsigned, Tree> original_models;

    Transcode(const Bytes& bytes, bool pack) : input(bytes, 12), packing(pack) {}

    Tree& model(unsigned key) {
        auto [it, fresh] = original_models.try_emplace(key);
        if (fresh) it->second.fill(1024);
        return it->second;
    }
    static void observe(uint16_t& p, unsigned bit) {
        p = bit ? p - (p >> 5) : p + ((2048 - p) >> 5);
    }
    // Floating-point scale bytes also have different distributions in each tensor.
    std::map<unsigned, std::array<Counts, 256>> tensor_history;
    unsigned previous_numeric = 0;

    unsigned metadata(unsigned key, unsigned width) {
        Tree& tree = model(key);
        bool numeric = key >= 100000;
        unsigned context = key;
        if (key == 102003) context += 500 * (1 + (previous_numeric >> 6));
        auto& local = tensor_history[numeric ? context : 0];
        unsigned node = 1;
        for (unsigned i = 0; i < width; ++i) {
            uint16_t& original = tree[node];
            uint16_t prob = original;
            if (numeric) {
                unsigned prior = key >= 102000 ? 48 : 16;
                prob = local[node].with_prior(original, prior);
            }
            unsigned bit = input.read(packing ? original : prob);
            output.put(packing ? prob : original, bit);
            observe(original, bit);
            if (numeric) local[node].add(bit);
            node = 2 * node + bit;
        }
        unsigned value = node - (1u << width);
        if (numeric) previous_numeric = value;
        return value;
    }
    unsigned original_weight(unsigned value) {
        Tree& tree = model(100001);
        unsigned node = 1;
        for (int shift = 3; shift >= 0; --shift) {
            uint16_t& p = tree[node];
            unsigned bit = packing ? input.read(p) : ((value >> shift) & 1);
            if (!packing) output.put(p, bit);
            observe(p, bit);
            node = 2 * node + bit;
        }
        require(node - 16 < 15, "invalid quantized weight");
        return node - 16;
    }

    // A row or column's earlier magnitude planes are available in full before
    // either side processes the next plane. No row or column table is shipped.
    void weights(size_t count, unsigned width) {
        require(width != 0 && count % width == 0 && count > 0, "invalid matrix");
        const size_t height = count / width;
        Bytes values(count), prefix(count, 0);
        if (packing) {
            for (uint8_t& value : values) value = original_weight(0);
        }
        for (int plane = 2; plane >= -1; --plane) {
            const bool sign = plane == -1;
            std::vector<int64_t> row_sum(height), col_sum(width);
            int64_t total = 0;
            for (size_t i = 0; i < count; ++i) {
                row_sum[i / width] += prefix[i];
                col_sum[i % width] += prefix[i];
                total += prefix[i];
            }
            auto bucket = [&](int64_t sum, int64_t length) {
                int64_t cells = static_cast<int64_t>(count);
                int64_t unit = sign ? 1 : (1 << (plane + 1));
                int64_t relative = (sum * cells - total * length) * 6 * unit;
                return unsigned(std::clamp<int64_t>(8 + relative / (cells * length), 0, 15));
            };
            std::vector<unsigned> row_group(height), col_group(width);
            for (size_t row = 0; row < height; ++row) row_group[row] = bucket(row_sum[row], width);
            for (unsigned col = 0; col < width; ++col) col_group[col] = bucket(col_sum[col], height);

            using TreeCounts = std::array<Counts, 8>;
            TreeCounts overall{};
            std::array<TreeCounts, 16> row_groups{}, col_groups{};
            std::vector<TreeCounts> rows(height), columns(width);
            for (size_t i = 0; i < count; ++i) {
                unsigned group = prefix[i];
                if (sign && group == 0) {
                    values[i] = 7;
                    continue;
                }
                size_t row = i / width, col = i % width;
                Counts& global = overall[group];
                Counts& row_context = row_groups[row_group[row]][group];
                Counts& col_context = col_groups[col_group[col]][group];
                Counts& row_count = rows[row][group];
                Counts& col_count = columns[col][group];
                int base = global.with_prior(1024, 4);
                int prob = base;
                if (!sign) {
                    prob = std::clamp(row_context.with_prior(base, 160) +
                                      col_context.with_prior(base, 160) - base, 1, 2047);
                }
                int adjustment = row_count.with_prior(base, 256) + col_count.with_prior(base, 256) - 2 * base;
                prob = std::clamp(prob + (sign ? adjustment : adjustment / 2), 1, 2047);
                unsigned bit;
                if (packing) {
                    int weight = int(values[i]) - 7;
                    bit = sign ? (weight > 0) : ((std::abs(weight) >> plane) & 1);
                    output.put(uint16_t(prob), bit);
                } else {
                    bit = input.read(uint16_t(prob));
                }
                if (sign) values[i] = uint8_t(bit ? 7 + group : 7 - group);
                else prefix[i] = uint8_t(2 * group + bit);
                global.add(bit);
                row_context.add(bit);
                col_context.add(bit);
                row_count.add(bit);
                col_count.add(bit);
            }
        }
        if (!packing) {
            for (uint8_t value : values) original_weight(value);
        }
    }

    void tensor_stream(uint32_t tensors) {
        unsigned previous = 0;
        auto meta = [&] { previous = metadata(previous,8); return previous; };
        for (uint32_t t = 0; t < tensors; ++t) {
            unsigned name_size = meta(), suffix = 0;
            for (unsigned i = 0; i < name_size; ++i) suffix = ((suffix << 8) | metadata(256 + suffix,8)) & 65535;
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
            tensor_history.clear();
            previous_numeric = 0;
            if (encoding == 1) {
                weights(count, columns);
            } else if (encoding == 2) {
                for (size_t i=0; i<count; ++i) { unsigned hi = metadata(100002,8); metadata(101000+hi,8); }
            } else if (encoding == 3) {
                for (size_t i=0; i<count*4; ++i) metadata(102000+(i%4),8);
            } else if (encoding == 0) {
                unsigned sizes[] = {1,2,4,4};
                for (size_t i=0; i<count*sizes[type]; ++i) metadata(100000,8);
            } else require(encoding == 4 || encoding == 5, "unknown tensor encoding");
        }
        output.finish();
    }
};

#include "events.hpp"

int main(int argc, char** argv) try {
    require(argc == 4, "usage: weight15 pack|unpack INPUT OUTPUT");
    std::string action = argv[1];
    bool pack = action == "pack";
    std::ifstream input(argv[2], std::ios::binary);
    require(bool(input), "cannot open input");
    Bytes bytes((std::istreambuf_iterator<char>(input)), {});
    if (action == "encode-prefix" || action == "decode-prefix") {
        Bytes prefix = transcode_events(bytes, action == "encode-prefix");
        std::ofstream dest(argv[3], std::ios::binary);
        dest.write(reinterpret_cast<const char*>(prefix.data()), prefix.size());
        dest.close();
        require(bool(dest), "output write failed");
        return 0;
    }
    require(pack || action == "unpack", "unknown action");
    std::string original = "FX2TFWC2", planes = "FT8PLN02";
    const auto& expected = pack ? original : planes;
    require(bytes.size() >= 17 && std::equal(expected.begin(), expected.end(), bytes.begin()), "wrong weight format");
    uint32_t tensors = u32(bytes, 8);
    require(tensors > 0 && tensors <= 65536, "invalid tensor count");
    Transcode coder(bytes, pack);
    coder.tensor_stream(tensors);
    std::ofstream dest(argv[3], std::ios::binary);
    const auto& magic = pack ? planes : original;
    dest.write(magic.data(), 8);
    dest.write(reinterpret_cast<const char*>(bytes.data() + 8), 4);
    dest.write(reinterpret_cast<const char*>(coder.output.out.data()), coder.output.out.size());
    dest.close();
    require(bool(dest), "output write failed");
    return 0;
} catch (const std::exception& err) {
    std::cerr << err.what() << '\n';
    return 1;
}

