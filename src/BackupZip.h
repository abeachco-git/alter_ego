#pragma once
// Dependency-free ZIP (stored entries, method 0) for portable Alter Ego backups.
// Archives produced here are standard ZIP files recognized by Windows Explorer.
// The reader deliberately rejects encrypted, compressed, ZIP64, unsafe paths,
// duplicate filenames, corrupt CRCs, and excessively large archives.
#include "AtomicFile.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace AlterEgo { namespace BackupZip {

using Entries = std::map<std::string, std::string>;
static constexpr size_t kMaxArchiveBytes = 256u * 1024u * 1024u;
static constexpr size_t kMaxEntryBytes = 48u * 1024u * 1024u;
static constexpr size_t kMaxEntries = 512;

inline void Put16(std::string& out, uint16_t x) {
    out.push_back(static_cast<char>(x & 0xff));
    out.push_back(static_cast<char>((x >> 8) & 0xff));
}
inline void Put32(std::string& out, uint32_t x) {
    Put16(out, static_cast<uint16_t>(x));
    Put16(out, static_cast<uint16_t>(x >> 16));
}
inline uint16_t Get16(const std::string& s, size_t p) {
    return static_cast<uint16_t>(static_cast<uint8_t>(s[p])) |
        (static_cast<uint16_t>(static_cast<uint8_t>(s[p+1])) << 8);
}
inline uint32_t Get32(const std::string& s, size_t p) {
    return static_cast<uint32_t>(Get16(s, p)) |
        (static_cast<uint32_t>(Get16(s, p + 2)) << 16);
}
inline bool Has(const std::string& s, size_t p, size_t bytes) {
    return p <= s.size() && bytes <= s.size() - p;
}
inline uint32_t CRC32(const std::string& bytes) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char byte : bytes) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}
inline bool SafeName(const std::string& name) {
    if (name.empty() || name.size() > 240 || name[0] == '/' ||
        name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos ||
        name.find("..") != std::string::npos) return false;
    for (unsigned char c : name) {
        if (c < 32 || c == 127) return false;
    }
    return true;
}

inline bool ReadBytes(const std::string& path, std::string& out,
                      std::string& error, size_t maximum = kMaxArchiveBytes) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) { error = "Could not open file."; return false; }
    std::streamoff length = stream.tellg();
    if (length < 0 || static_cast<uint64_t>(length) > maximum) {
        error = "File exceeds the backup size limit."; return false;
    }
    out.resize(static_cast<size_t>(length));
    stream.seekg(0);
    if (length && !stream.read(&out[0], length)) {
        error = "Could not read full file."; return false;
    }
    return true;
}

inline bool WriteStored(const Entries& entries, const std::string& path, std::string& error) {
    if (entries.empty() || entries.size() > kMaxEntries) {
        error = "Invalid number of archive files."; return false;
    }
    std::string local, central;
    struct Central { std::string name; uint32_t crc, size, offset; };
    std::vector<Central> recorded;
    for (const auto& [name, data] : entries) {
        if (!SafeName(name) || name.size() > 65535 ||
            data.size() > kMaxEntryBytes ||
            local.size() + data.size() + 80 + name.size() > kMaxArchiveBytes) {
            error = "Archive has an unsafe filename or oversized file."; return false;
        }
        Central info{name, CRC32(data), static_cast<uint32_t>(data.size()),
                     static_cast<uint32_t>(local.size())};
        Put32(local, 0x04034b50);
        Put16(local, 20); Put16(local, 0); Put16(local, 0);
        Put16(local, 0); Put16(local, 0); // no compression, no timestamp
        Put32(local, info.crc); Put32(local, info.size); Put32(local, info.size);
        Put16(local, static_cast<uint16_t>(name.size())); Put16(local, 0);
        local += name; local += data;
        recorded.push_back(info);
    }
    const size_t centralOffset = local.size();
    for (const auto& info : recorded) {
        Put32(central, 0x02014b50);
        Put16(central, 20); Put16(central, 20);
        Put16(central, 0); Put16(central, 0);
        Put16(central, 0); Put16(central, 0);
        Put32(central, info.crc);
        Put32(central, info.size); Put32(central, info.size);
        Put16(central, static_cast<uint16_t>(info.name.size()));
        Put16(central, 0); Put16(central, 0); Put16(central, 0);
        Put16(central, 0); Put32(central, 0); Put32(central, info.offset);
        central += info.name;
    }
    if (local.size() + central.size() + 22 > kMaxArchiveBytes) {
        error = "Archive is too large."; return false;
    }
    local += central;
    Put32(local, 0x06054b50);
    Put16(local, 0); Put16(local, 0);
    Put16(local, static_cast<uint16_t>(recorded.size()));
    Put16(local, static_cast<uint16_t>(recorded.size()));
    Put32(local, static_cast<uint32_t>(central.size()));
    Put32(local, static_cast<uint32_t>(centralOffset));
    Put16(local, 0);
    return Storage::ReplaceFile(std::filesystem::u8path(path),local,error);
}

inline bool ReadStored(const std::string& bytes, Entries& entries, std::string& error) {
    entries.clear();
    if (bytes.size() > kMaxArchiveBytes || bytes.size() < 22) {
        error = "Invalid or oversized ZIP archive."; return false;
    }
    const size_t search = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
    size_t eocd = bytes.size();
    for (size_t p = bytes.size() - 22;; --p) {
        if (Get32(bytes, p) == 0x06054b50u &&
            Has(bytes, p, 22) && p + 22 + Get16(bytes, p + 20) == bytes.size()) {
            eocd = p; break;
        }
        if (p == search) break;
    }
    if (eocd == bytes.size()) { error = "ZIP central directory missing."; return false; }
    const uint16_t count = Get16(bytes, eocd + 10);
    const uint16_t diskCount = Get16(bytes, eocd + 8);
    if (Get16(bytes, eocd + 4) || Get16(bytes, eocd + 6) ||
        count != diskCount || count > kMaxEntries || !count) {
        error = "Multi-disk or unsupported ZIP archive."; return false;
    }
    size_t p = Get32(bytes, eocd + 16);
    const size_t end = p + static_cast<size_t>(Get32(bytes, eocd + 12));
    if (end > eocd || p > end) {
        error = "Invalid ZIP central directory offsets."; return false;
    }
    size_t total = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (!Has(bytes, p, 46) || Get32(bytes, p) != 0x02014b50u) {
            error = "Invalid ZIP directory entry."; return false;
        }
        const uint16_t flags = Get16(bytes, p + 8);
        const uint16_t method = Get16(bytes, p + 10);
        if ((flags & 1u) || method != 0) {
            error = method != 0 ?
                "Compressed ZIP entries are not supported. Use an Alter Ego backup ZIP or import JSON files directly." :
                "Encrypted ZIP archives are not supported.";
            return false;
        }
        const uint32_t crc = Get32(bytes, p + 16);
        const size_t compressed = Get32(bytes, p + 20);
        const size_t original = Get32(bytes, p + 24);
        const size_t nameLen = Get16(bytes, p + 28);
        const size_t extraLen = Get16(bytes, p + 30);
        const size_t commentLen = Get16(bytes, p + 32);
        const size_t localOffset = Get32(bytes, p + 42);
        if (!Has(bytes, p, 46 + nameLen + extraLen + commentLen) ||
            compressed != original || original > kMaxEntryBytes ||
            total + original > kMaxArchiveBytes ||
            nameLen == 0) {
            error = "ZIP entry is malformed or too large."; return false;
        }
        const std::string name = bytes.substr(p + 46, nameLen);
        if (!SafeName(name) || entries.count(name) ||
            !Has(bytes, localOffset, 30) ||
            Get32(bytes, localOffset) != 0x04034b50u) {
            error = "Unsafe or duplicate ZIP entry."; return false;
        }
        const uint16_t localNameLen = Get16(bytes, localOffset + 26);
        const uint16_t localExtraLen = Get16(bytes, localOffset + 28);
        const size_t payload = localOffset + 30u + localNameLen + localExtraLen;
        if (!Has(bytes, localOffset + 30, localNameLen + localExtraLen) ||
            bytes.compare(localOffset + 30, localNameLen, name) != 0 ||
            !Has(bytes, payload, original) || payload + original > p) {
            error = "ZIP local entry does not match directory."; return false;
        }
        std::string data = bytes.substr(payload, original);
        if (CRC32(data) != crc) { error = "ZIP CRC mismatch."; return false; }
        entries.emplace(name, std::move(data));
        total += original;
        p += 46 + nameLen + extraLen + commentLen;
    }
    if (p != end) { error = "Unexpected ZIP directory data."; return false; }
    return true;
}
}} // AlterEgo::BackupZip
