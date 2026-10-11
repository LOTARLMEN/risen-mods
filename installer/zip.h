#pragma once

// Minimal zip reader for the release archives (made by PowerShell Compress-Archive): central directory,
// "stored" and "deflate" entries, CRC-32 check. No zip64, encryption or multi-disk archives.

#include <string>
#include <vector>

struct ZipEntry {
    std::string name; // with '/' as written in the archive, e.g. "bin/mods/speed.dll"
    std::vector<unsigned char> data;
};

// All file entries (folders skipped). False + error text on a broken archive.
bool ZipExtract(const std::vector<unsigned char>& zip, std::vector<ZipEntry>* entries, std::string* error);

// Raw deflate stream (RFC 1951) -> out. False on broken data.
bool Inflate(const unsigned char* in, size_t size, std::vector<unsigned char>* out);
