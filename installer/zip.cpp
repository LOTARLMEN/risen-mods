#include "zip.h"

#include <cstdint>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------------------------
// Inflate (RFC 1951), canonical Huffman decoding one bit at a time - plenty for a few MB.

struct Huffman {
    uint16_t counts[16];   // codes per length
    uint16_t symbols[320]; // symbols ordered by code
};

struct BitReader {
    const unsigned char* data;
    size_t size, pos = 0;
    uint32_t bits = 0;
    int count = 0;
    bool overrun = false;

    int Bit()
    {
        if (count == 0) {
            if (pos >= size) {
                overrun = true;
                return 0;
            }
            bits = data[pos++];
            count = 8;
        }
        int b = bits & 1;
        bits >>= 1;
        --count;
        return b;
    }

    uint32_t Bits(int n)
    {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i)
            v |= static_cast<uint32_t>(Bit()) << i;
        return v;
    }
};

bool Build(Huffman* h, const uint8_t* lengths, int n)
{
    memset(h->counts, 0, sizeof(h->counts));
    for (int i = 0; i < n; ++i)
        ++h->counts[lengths[i]];
    h->counts[0] = 0;
    uint16_t offsets[16];
    offsets[1] = 0;
    for (int len = 1; len < 15; ++len)
        offsets[len + 1] = offsets[len] + h->counts[len];
    for (int i = 0; i < n; ++i)
        if (lengths[i])
            h->symbols[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
    return true;
}

int Decode(BitReader& in, const Huffman& h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= in.Bit();
        int count = h.counts[len];
        if (code - first < count)
            return h.symbols[index + code - first];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        if (in.overrun)
            return -1;
    }
    return -1;
}

const uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool Codes(BitReader& in, std::vector<unsigned char>* out, const Huffman& lit, const Huffman& dist)
{
    for (;;) {
        int sym = Decode(in, lit);
        if (sym < 0)
            return false;
        if (sym < 256) {
            out->push_back(static_cast<unsigned char>(sym));
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29)
                return false;
            size_t len = kLengthBase[sym] + in.Bits(kLengthExtra[sym]);
            int d = Decode(in, dist);
            if (d < 0 || d >= 30)
                return false;
            size_t back = kDistBase[d] + in.Bits(kDistExtra[d]);
            if (back > out->size() || in.overrun)
                return false;
            size_t from = out->size() - back;
            for (size_t i = 0; i < len; ++i)
                out->push_back((*out)[from + i]);
        }
    }
}

bool Dynamic(BitReader& in, std::vector<unsigned char>* out)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int nlen = in.Bits(5) + 257, ndist = in.Bits(5) + 1, ncode = in.Bits(4) + 4;
    if (nlen > 286 || ndist > 30)
        return false;
    uint8_t lengths[320] = {};
    for (int i = 0; i < ncode; ++i)
        lengths[order[i]] = static_cast<uint8_t>(in.Bits(3));
    Huffman lencode, lit, dist;
    Build(&lencode, lengths, 19);
    memset(lengths, 0, sizeof(lengths));
    for (int i = 0; i < nlen + ndist;) {
        int sym = Decode(in, lencode);
        if (sym < 0)
            return false;
        if (sym < 16) {
            lengths[i++] = static_cast<uint8_t>(sym);
            continue;
        }
        uint8_t value = 0;
        int repeat;
        if (sym == 16) {
            if (i == 0)
                return false;
            value = lengths[i - 1];
            repeat = 3 + in.Bits(2);
        } else if (sym == 17) {
            repeat = 3 + in.Bits(3);
        } else {
            repeat = 11 + in.Bits(7);
        }
        if (i + repeat > nlen + ndist)
            return false;
        while (repeat--)
            lengths[i++] = value;
    }
    Build(&lit, lengths, nlen);
    Build(&dist, lengths + nlen, ndist);
    return Codes(in, out, lit, dist);
}

bool Fixed(BitReader& in, std::vector<unsigned char>* out)
{
    static Huffman lit, dist;
    static bool ready;
    if (!ready) {
        uint8_t lengths[288];
        int i = 0;
        for (; i < 144; ++i) lengths[i] = 8;
        for (; i < 256; ++i) lengths[i] = 9;
        for (; i < 280; ++i) lengths[i] = 7;
        for (; i < 288; ++i) lengths[i] = 8;
        Build(&lit, lengths, 288);
        for (i = 0; i < 30; ++i) lengths[i] = 5;
        Build(&dist, lengths, 30);
        ready = true;
    }
    return Codes(in, out, lit, dist);
}

bool Stored(BitReader& in, std::vector<unsigned char>* out)
{
    in.count = 0; // skip to the byte boundary
    if (in.pos + 4 > in.size)
        return false;
    uint16_t len = static_cast<uint16_t>(in.data[in.pos] | in.data[in.pos + 1] << 8);
    uint16_t nlen = static_cast<uint16_t>(in.data[in.pos + 2] | in.data[in.pos + 3] << 8);
    in.pos += 4;
    if (len != static_cast<uint16_t>(~nlen) || in.pos + len > in.size)
        return false;
    out->insert(out->end(), in.data + in.pos, in.data + in.pos + len);
    in.pos += len;
    return true;
}

uint32_t Crc32(const unsigned char* p, size_t n)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

uint16_t U16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t U32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

} // namespace

bool Inflate(const unsigned char* data, size_t size, std::vector<unsigned char>* out)
{
    BitReader in{data, size};
    for (;;) {
        int last = in.Bit();
        int type = static_cast<int>(in.Bits(2));
        bool ok = type == 0 ? Stored(in, out) : type == 1 ? Fixed(in, out) : type == 2 ? Dynamic(in, out) : false;
        if (!ok || in.overrun)
            return false;
        if (last)
            return true;
    }
}

bool ZipExtract(const std::vector<unsigned char>& zip, std::vector<ZipEntry>* entries, std::string* error)
{
    const unsigned char* z = zip.data();
    size_t n = zip.size();
    // End of central directory: last "PK\5\6" (a comment may follow it).
    size_t eocd = n >= 22 ? n - 22 : 0;
    while (eocd > 0 && U32(z + eocd) != 0x06054b50)
        --eocd;
    if (n < 22 || U32(z + eocd) != 0x06054b50) {
        *error = "not a zip archive";
        return false;
    }
    size_t count = U16(z + eocd + 10), pos = U32(z + eocd + 16);
    for (size_t i = 0; i < count; ++i) {
        if (pos + 46 > n || U32(z + pos) != 0x02014b50) {
            *error = "broken central directory";
            return false;
        }
        const unsigned char* c = z + pos;
        uint16_t method = U16(c + 10), nameLen = U16(c + 28), extraLen = U16(c + 30), commentLen = U16(c + 32);
        uint32_t crc = U32(c + 16), packed = U32(c + 20), size = U32(c + 24), local = U32(c + 42);
        if (pos + 46 + nameLen > n) {
            *error = "broken central directory";
            return false;
        }
        ZipEntry e;
        e.name.assign(reinterpret_cast<const char*>(c + 46), nameLen);
        for (char& ch : e.name)
            if (ch == '\\')
                ch = '/'; // old Compress-Archive writes backslashes
        pos += 46 + nameLen + extraLen + commentLen;
        if (e.name.empty() || e.name.back() == '/')
            continue; // folder

        if (local + 30 > n || U32(z + local) != 0x04034b50) {
            *error = "broken entry " + e.name;
            return false;
        }
        size_t start = local + 30 + U16(z + local + 26) + U16(z + local + 28);
        if (start + packed > n) {
            *error = "truncated entry " + e.name;
            return false;
        }
        if (method == 0) {
            e.data.assign(z + start, z + start + packed);
        } else if (method == 8) {
            e.data.reserve(size);
            if (!Inflate(z + start, packed, &e.data)) {
                *error = "can't unpack " + e.name;
                return false;
            }
        } else {
            *error = "unsupported compression in " + e.name;
            return false;
        }
        if (e.data.size() != size || Crc32(e.data.data(), e.data.size()) != crc) {
            *error = "checksum mismatch in " + e.name;
            return false;
        }
        entries->push_back(std::move(e));
    }
    return true;
}
