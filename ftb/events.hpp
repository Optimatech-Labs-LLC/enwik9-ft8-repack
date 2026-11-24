// SPDX-License-Identifier: GPL-3.0-only
// Integer probability models for the NRV2B decisions in the five ft8 blocks.
#include <memory>
#include <unordered_map>

struct EventModel {
    using Tree = std::array<Counts, 256>;
    std::unordered_map<uint64_t, Tree> tables;
    RangeOut out;
    std::unique_ptr<RangeIn> in;

    Tree& tree(unsigned field, unsigned family, unsigned context) {
        return tables[(uint64_t(field) << 40) | (uint64_t(family) << 32) | context];
    }

    unsigned bit(unsigned value, unsigned probability) {
        if (in) return in->read(probability);
        value &= 1;
        out.put(probability, value);
        return value;
    }

    unsigned field(unsigned field_id, unsigned value, unsigned bits,
                   unsigned first, unsigned second, unsigned joint, unsigned maximum = UINT32_MAX) {
        auto& global = tree(field_id, 0, 0);
        auto& a = tree(field_id, 1, first);
        auto& b = tree(field_id, 2, second);
        auto& c = tree(field_id, 3, joint);
        // Counts grow independently in each context. Copy lengths, literals and
        // distances use different mixtures. These seven rows travel in the decoder.
        static constexpr unsigned prior[] = {10, 2, 2, 1, 16, 24, 64};
        static constexpr unsigned mix[][3] = {{1, 0, 3}, {1, 3, 0}, {0, 2, 2}, {0, 4, 0}, {0, 1, 3}, {0, 0, 4}, {2, 0, 2}};
        unsigned family = std::min(6u, field_id);
        unsigned strength = prior[family];
        const auto& w = mix[family];
        unsigned node = 1;
        for (unsigned i = bits; i > 0; --i) {
            unsigned base = global[node].with_prior(1024, 3);
            unsigned pa = a[node].with_prior(base, strength);
            unsigned pb = b[node].with_prior(base, strength);
            unsigned pc = c[node].with_prior((2 * pa + pb) / 3, strength);
            unsigned first_one = (2 * node + 1 - (1u << (bits - i + 1))) << (i - 1);
            unsigned next = first_one > maximum ? 0 :
                bit(value >> (i - 1), (pa * w[0] + pb * w[1] + pc * w[2]) / 4);
            global[node].add(next);
            a[node].add(next);
            b[node].add(next);
            c[node].add(next);
            node = 2 * node + next;
        }
        return node - (1u << bits);
    }

    unsigned uniform(unsigned value, unsigned bits) {
        unsigned result = 0;
        for (unsigned i = bits; i > 0; --i) {
            result = 2 * result + bit(value >> (i - 1), 1024);
        }
        return result;
    }
};

unsigned floor_log2(unsigned value) {
    unsigned exponent = 0;
    while (value > 1) {
        value >>= 1;
        ++exponent;
    }
    return exponent;
}

void append32(Bytes& out, unsigned value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(uint8_t(value >> shift));
}

Bytes transcode_events(const Bytes& data, bool packing) {
    constexpr size_t header = 5 * 4;
    require(data.size() >= header, "missing NRV sizes");
    Bytes result(data.begin(), data.begin() + header);
    size_t cursor = header;
    EventModel model;
    if (!packing) model.in = std::make_unique<RangeIn>(data, header);
    for (unsigned block = 0; block < 5; ++block) {
        unsigned limit = u32(data, block * 4);
        require(limit > 0 && limit <= 1000000, "invalid expanded size");
        Bytes raw;
        raw.reserve(limit);
        unsigned distance = 1, previous_length = 0;
        std::vector<unsigned> recent;
        while (raw.size() < limit) {
            unsigned length = 0, value = 0;
            if (packing) {
                length = u32(data, cursor);
                value = u32(data, cursor + 4);
                cursor += 8;
            }
            unsigned pos = unsigned(raw.size());
            unsigned last_byte = pos ? raw[pos - 1] : 0;
            unsigned second_byte = pos > 1 ? raw[pos - 2] : 0;
            unsigned distance_hint = pos >= distance ? raw[pos - distance] : 0;
            unsigned prior = std::min(previous_length, 8u);
            unsigned run = model.field(0, length ? std::min(length - 1, 15u) : 0, 4,
                                       prior, last_byte, last_byte * 9 + prior);
            if (run == 15) {
                unsigned extra = packing ? length - 15 : 0;
                unsigned exponent = model.field(1, floor_log2(extra), 4, prior, block, 0);
                length = 15 + (1u << exponent) + model.uniform(extra, exponent);
            } else {
                length = run ? run + 1 : 0;
            }
            if (!length) {
                value = model.field(2, value, 8, last_byte, distance_hint, second_byte * 256 + last_byte);
                raw.push_back(uint8_t(value));
            } else {
                require(length <= limit - pos && pos > 0, "invalid copy size");
                unsigned group = std::min(length, 10u);
                bool repeat = model.field(3, value == 0, 1, group, prior, block * 16 + group);
                if (!repeat) {
                    unsigned rank = 15;
                    if (packing) {
                        auto it = std::find(recent.begin(), recent.end(), value);
                        if (it != recent.end()) rank = std::min(15u, unsigned(it - recent.begin()));
                    }
                    rank = model.field(4, rank, 4, group, prior, block * 16 + group);
                    if (rank < 15) {
                        require(rank < recent.size(), "invalid cached distance");
                        distance = recent[rank];
                    } else {
                        unsigned farthest = length == 2 ? std::min(pos, 0xd00u) : pos;
                        unsigned exponent = model.field(5, floor_log2(value), 5, group,
                            floor_log2(distance), group * 8 + std::min(7u, floor_log2(pos) / 3), floor_log2(farthest));
                        require(exponent < 21, "invalid distance width");
                        distance = 1u << exponent;
                        for (unsigned k = 0; k < exponent; ++k) {
                            unsigned low = model.field(6 + std::min(k, 16u), value >> k, 1,
                                std::min(exponent, 16u), std::min(k, 6u) * 16 + pos % 16,
                                group * 16 + distance % 16, distance + (1u << k) <= farthest ? 1u : 0u);
                            distance += low << k;
                        }
                    }
                    auto it = std::find(recent.begin(), recent.end(), distance);
                    if (it != recent.end()) recent.erase(it);
                    recent.insert(recent.begin(), distance);
                    if (recent.size() > 16) recent.pop_back();
                }
                value = repeat ? 0 : distance;
                require(distance > 0 && distance <= pos, "invalid copy distance");
                for (unsigned j = 0; j < length; ++j) raw.push_back(raw[raw.size() - distance]);
            }
            previous_length = length;
            if (!packing) {
                append32(result, length);
                append32(result, value);
            }
        }
    }
    if (packing) {
        require(cursor == data.size(), "unused NRV record");
        model.out.finish();
        result.insert(result.end(), model.out.out.begin(), model.out.out.end());
    }
    return result;
}
