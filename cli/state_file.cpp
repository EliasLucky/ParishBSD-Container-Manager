#include "state_file.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace parish {
namespace tbuild {

bool parse_state_file(const std::string& path,
                      std::map<std::string, std::string>& out,
                      std::string& error)
{
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) { error = "cannot open " + path + ": " + std::strerror(errno); return false; }
    out.clear();
    char* line = nullptr;
    size_t cap = 0;
    ssize_t n;
    int lineno = 0;
    while ((n = getline(&line, &cap, f)) >= 0) {
        ++lineno;
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        if (n == 0 || line[0] == '#') continue;
        std::string s(line);
        auto eq = s.find('=');
        if (eq == std::string::npos) {
            error = path + ":" + std::to_string(lineno) + ": missing '='";
            std::free(line);
            std::fclose(f);
            return false;
        }
        out[s.substr(0, eq)] = s.substr(eq + 1);
    }
    std::free(line);
    std::fclose(f);
    return true;
}

bool write_state_file_atomic(const std::string& path,
                             const std::map<std::string, std::string>& kv,
                             std::string& error)
{
    // Preserve existing mode; 0600 for new file (template.conf may carry
    // patch-level forensics before sanitization).
    mode_t mode = 0600;
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) mode = st.st_mode & 07777;

    std::string tmp = path + ".tmp." + std::to_string(::getpid());
    int fd = ::open(tmp.c_str(), O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, mode);
    if (fd < 0) {
        error = "open " + tmp + ": " + std::strerror(errno);
        return false;
    }
    // Deterministic key order for readability and stable diffs.
    static const char* order[] = {
        "name","kind","state","description.en","description.zh",
        "hidden","clusters","requested_clusters","applications",
        "extra_packages","compat","created","updated",
        "build_size_mb","current_base","provision"
    };
    std::map<std::string, std::string> remaining = kv;
    for (const char* k : order) {
        auto it = remaining.find(k);
        if (it == remaining.end()) continue;
        std::string line = it->first + "=" + it->second + "\n";
        if (::write(fd, line.data(), line.size()) != (ssize_t)line.size()) {
            error = "write " + tmp + ": " + std::strerror(errno);
            ::close(fd); ::unlink(tmp.c_str());
            return false;
        }
        remaining.erase(it);
    }
    for (const auto& [k, v] : remaining) {
        std::string line = k + "=" + v + "\n";
        if (::write(fd, line.data(), line.size()) != (ssize_t)line.size()) {
            error = "write " + tmp + ": " + std::strerror(errno);
            ::close(fd); ::unlink(tmp.c_str());
            return false;
        }
    }
    if (::fsync(fd) != 0) {
        error = "fsync " + tmp + ": " + std::strerror(errno);
        ::close(fd); ::unlink(tmp.c_str());
        return false;
    }
    if (::close(fd) != 0) {
        error = "close " + tmp + ": " + std::strerror(errno);
        ::unlink(tmp.c_str());
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        error = "rename " + tmp + " -> " + path + ": " + std::strerror(errno);
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace tbuild
}  // namespace parish