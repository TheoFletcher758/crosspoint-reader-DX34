#pragma once

#include <cstdint>
#include <string>

// Content-based fingerprint for book files.
// Produces a stable 64-bit hash from file size + sampled bytes so that
// moving a file to a different folder does not change its identity.
namespace BookFingerprint {

// Compute FNV-1a 64-bit fingerprint of a book file by sampling bytes at
// 5 offsets (0, size/4, size/2, 3*size/4, size-256). Returns 0 on error.
uint64_t compute(const std::string& filepath);

// Build the cache directory name for a book: "/<cacheBase>/<prefix>_<fingerprint>".
// Falls back to path-hash if the file cannot be read.
// If an old path-hash directory exists, it is lazily renamed.
std::string cacheDirName(const char* prefix, const std::string& filepath, const std::string& cacheBase);

}  // namespace BookFingerprint
