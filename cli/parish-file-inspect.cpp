// cli/parish-file-inspect.cpp
//
// Inspects an installer or archive file by magic bytes / ELF ABI headers
// (NOT just file extension) and verifies whether a target compartment has the
// required compatibility capabilities before installation proceeds.
//
// Exit codes:
//   0  compatible; prints JSON summary to stdout
//   1  incompatible with target compartment (prints human reason to stderr)
//   2  usage / invalid arguments or unreadable/symlink file

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

enum class FileKind {
    FreeBsdPkg,     // .pkg / .txz / ar archive
    WindowsPe,      // .exe (MZ + PE)
    WindowsMsi,     // .msi (OLE Compound Document D0 CF 11 E0)
    LinuxElf,       // ELF with Linux ABI or .deb / .rpm / .AppImage
    FreeBsdElf,     // ELF with FreeBSD ABI
    Archive,        // .tar.gz / .tar.xz / .tar.bz2 / .zip
    ShellScript,    // #!/... (.run / .bin)
    Unknown
};

const char* kind_to_string(FileKind k)
{
    switch (k) {
    case FileKind::FreeBsdPkg:  return "freebsd-pkg";
    case FileKind::WindowsPe:   return "windows-pe";
    case FileKind::WindowsMsi:  return "windows-msi";
    case FileKind::LinuxElf:    return "linux-binary";
    case FileKind::FreeBsdElf:  return "freebsd-elf";
    case FileKind::Archive:     return "archive";
    case FileKind::ShellScript: return "installer-script";
    case FileKind::Unknown:     return "unknown";
    }
    return "unknown";
}

bool has_suffix_ci(const std::string& s, const std::string& suffix)
{
    if (s.size() < suffix.size()) return false;
    size_t off = s.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = s[off + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool contains_bytes(const std::vector<uint8_t>& buf, const char* needle)
{
    size_t nlen = std::strlen(needle);
    if (buf.size() < nlen) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(needle);
    return std::search(buf.begin(), buf.end(), p, p + nlen) != buf.end();
}

FileKind classify_elf(const std::vector<uint8_t>& buf, const std::string& path)
{
    if (buf.size() >= 8) {
        uint8_t osabi = buf[7];
        if (osabi == 9) return FileKind::FreeBsdElf; // ELFOSABI_FREEBSD
        if (osabi == 3) return FileKind::LinuxElf;   // ELFOSABI_LINUX
    }
    if (has_suffix_ci(path, ".appimage")) return FileKind::LinuxElf;

    // When EI_OSABI == 0 (ELFOSABI_NONE), inspect ELF interpreter / ABI note strings
    if (contains_bytes(buf, "/libexec/ld-elf.so") || contains_bytes(buf, "FreeBSD")) {
        return FileKind::FreeBsdElf;
    }
    if (contains_bytes(buf, "ld-linux") || contains_bytes(buf, "GNU")) {
        return FileKind::LinuxElf;
    }
    return FileKind::FreeBsdElf;
}

FileKind detect_magic(const std::vector<uint8_t>& buf, const std::string& path)
{
    if (buf.size() >= 4) {
        if (buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
            return classify_elf(buf, path);
        }
        if (buf[0] == 'M' && buf[1] == 'Z') {
            return FileKind::WindowsPe;
        }
        if (buf[0] == 0xED && buf[1] == 0xAB && buf[2] == 0xEE && buf[3] == 0xDB) {
            return FileKind::LinuxElf;
        }
        if (buf[0] == 'P' && buf[1] == 'K' && buf[2] == 0x03 && buf[3] == 0x04) {
            return FileKind::Archive;
        }
    }

    if (buf.size() >= 8) {
        static const uint8_t ole[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
        if (std::memcmp(buf.data(), ole, 8) == 0) {
            return FileKind::WindowsMsi;
        }
        if (std::memcmp(buf.data(), "!<arch>\n", 8) == 0) {
            return has_suffix_ci(path, ".deb") ? FileKind::LinuxElf : FileKind::Archive;
        }
    }

    if (buf.size() >= 6) {
        static const uint8_t xz[6] = {0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00};
        if (std::memcmp(buf.data(), xz, 6) == 0) {
            return (has_suffix_ci(path, ".pkg") || has_suffix_ci(path, ".txz"))
                ? FileKind::FreeBsdPkg
                : FileKind::Archive;
        }
    }

    if (buf.size() >= 4) {
        if (buf[0] == 0x28 && buf[1] == 0xB5 && buf[2] == 0x2F && buf[3] == 0xFD) {
            return has_suffix_ci(path, ".pkg") ? FileKind::FreeBsdPkg : FileKind::Archive;
        }
    }

    if (buf.size() >= 2) {
        if (buf[0] == 0x1F && buf[1] == 0x8B) return FileKind::Archive;
        if (buf[0] == 'B' && buf[1] == 'Z')   return FileKind::Archive;
        if (buf[0] == '#' && buf[1] == '!')   return FileKind::ShellScript;
    }

    return FileKind::Unknown;
}

bool contains_csv_cap(const std::string& csv, const std::string& target)
{
    std::string cur;
    for (char c : csv) {
        if (c == ',') {
            if (cur == target) return true;
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    return cur == target;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: parish-file-inspect <file> [capabilities_csv]\n");
        return 2;
    }

    const std::string path = argv[1];
    const std::string caps = (argc == 3) ? argv[2] : "";

    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        std::fprintf(stderr, "parish-file-inspect: '%s' is not a readable regular file\n", path.c_str());
        return 2;
    }

    int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        std::fprintf(stderr, "parish-file-inspect: cannot open '%s'\n", path.c_str());
        return 2;
    }

    std::vector<uint8_t> buf(4096);
    ssize_t n = ::read(fd, buf.data(), buf.size());
    ::close(fd);
    if (n < 0) {
        std::fprintf(stderr, "parish-file-inspect: read error\n");
        return 2;
    }
    buf.resize(static_cast<size_t>(n));

    FileKind kind = detect_magic(buf, path);
    const char* req = "-";
    if (kind == FileKind::WindowsPe || kind == FileKind::WindowsMsi) {
        req = "compat-windows";
    } else if (kind == FileKind::LinuxElf) {
        req = "compat-linux";
    }

    if (!caps.empty() && std::strcmp(req, "-") != 0 && !contains_csv_cap(caps, req)) {
        std::fprintf(stderr,
                     "Incompatible file (%s): compartment lacks required capability '%s'\n",
                     kind_to_string(kind), req);
        return 1;
    }

    std::printf("{\"kind\":\"%s\",\"requires\":\"%s\"}\n", kind_to_string(kind), req);
    return 0;
}
