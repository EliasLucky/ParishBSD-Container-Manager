#include "template_build.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace parish::tbuild;

static bool capture_once(Ops& ops, const std::vector<std::string>& argv, std::string& out) {
    if (ops.run_cmd_capture(argv, out) != 0) return false;
    while (!out.empty() && (out.back()=='\n' || out.back()=='\r' || out.back()==' ')) {
        out.pop_back();
    }
    return !out.empty();
}

static std::string get_version(Ops& ops) {
    std::string v;
    if (!capture_once(ops, {"/bin/freebsd-version", "-u"}, v)) return "";
    auto p = v.find("-p");
    if (p != std::string::npos) v = v.substr(0, p);
    return v;
}

static std::string get_arch(Ops& ops) {
    std::string a;
    if (!capture_once(ops, {"/usr/bin/uname", "-m"}, a)) return "";
    return a;
}

int main(int argc, char** argv) {
    std::string name;
    std::string source = "shipped";
    std::string templates_override;
    std::string state_override;
    std::string base_cache_override;
    std::string cache_dir = "/var/cache/parishbsd";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a.rfind("--source=", 0) == 0)        source = a.substr(9);
        else if (a.rfind("--templates-dir=", 0) == 0) templates_override = a.substr(16);
        else if (a.rfind("--state-dir=", 0) == 0)     state_override = a.substr(12);
        else if (a.rfind("--base-cache=", 0) == 0)    base_cache_override = a.substr(13);
        else if (a.rfind("--cache-dir=", 0) == 0)     cache_dir = a.substr(12);
        else if (!a.empty() && a[0] != '-' && name.empty()) name = a;
        else {
            std::fprintf(stderr,
                "usage: parish-template-build <name> "
                "[--source=shipped|user] [--templates-dir=<path>] "
                "[--state-dir=<path>] [--cache-dir=<path>] "
                "[--base-cache=<dataset>]\n");
            return 2;
        }
    }
    if (name.empty()) {
        std::fprintf(stderr,
            "usage: parish-template-build <name> [--source=shipped|user] ...\n");
        return 2;
    }

    std::string source_dir;
    if      (source == "shipped") source_dir = "/usr/local/share/parishbsd/templates";
    else if (source == "user")    source_dir = "/var/db/parishbsd/templates";
    else {
        std::fprintf(stderr, "parish: --source must be 'shipped' or 'user'\n");
        return 2;
    }
    if (!templates_override.empty()) source_dir = templates_override;

    std::string state_dir = "/var/db/parishbsd/templates";
    if (!state_override.empty()) state_dir = state_override;

    Ops ops;
    std::string version = get_version(ops);
    std::string arch    = get_arch(ops);
    if (version.empty() || arch.empty()) {
        std::fprintf(stderr,
            "parish: cannot determine host version/arch "
            "(freebsd-version or uname -m failed)\n");
        return 1;
    }

    std::string base_cache = base_cache_override;
    // base_cache may be empty; build_template will derive + construct.

    BuildResult r = build_template(
        name, source_dir, state_dir, cache_dir, version, arch, base_cache,
        "/usr/local/libexec/parishbsd/templates/clusters.tsv",
        "/var/log/parishbsd",
        "/var/run/parishbsd",
        ops);

    for (const auto& s : r.steps) {
        const char* mark = (s.outcome == Outcome::Ok) ? "OK  " : "FAIL";
        std::printf("  [%s] %s%s%s\n", mark, s.name.c_str(),
                    s.detail.empty() ? "" : ": ", s.detail.c_str());
    }
    if (r.succeeded) {
        std::printf("Template '%s' built successfully. Log: %s\n",
                    name.c_str(), r.log_path.c_str());
        return 0;
    }
    std::fprintf(stderr, "Build failed. Log: %s\n", r.log_path.c_str());
    return 1;
}