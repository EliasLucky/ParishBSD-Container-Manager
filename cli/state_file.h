#ifndef PARISH_STATE_FILE_H
#define PARISH_STATE_FILE_H

#include <map>
#include <string>

namespace parish {
namespace tbuild {

// Parse a template.conf. Returns a key→value map.
// Fails if the file is unreadable or malformed (missing '=' on any non-blank line).
bool parse_state_file(const std::string& path,
                      std::map<std::string, std::string>& out,
                      std::string& error);

// Atomic write: tmp (same dir) + fsync + rename. Preserves existing file's
// mode if it exists; uses 0600 for a new file.
// Returns false and sets error on failure.
bool write_state_file_atomic(const std::string& path,
                             const std::map<std::string, std::string>& kv,
                             std::string& error);

}  // namespace tbuild
}  // namespace parish

#endif