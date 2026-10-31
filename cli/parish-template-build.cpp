#include "template_build.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace parish::tbuild;

int main(int argc, char** argv) {
    std::string name;
    std::string source = "shipped";  // default
    std::string templates_override;
    std::string state_override;
    std::string base_cache_override;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a.rfind("--source=", 0) == 0)         source = a.substr(9);
        else if (a.rfind("--templates-dir=", 0) == 0)  templates_override = a.substr(16);
        else if (a.rfind("--state-dir=", 0) == 0)      state_override = a.substr(12);
        else if (a.rfind("--base-cache=", 0) == 0)     base_cache_override = a.substr(13);
        else if (!a.empty() && a[0] != '-' && name.empty()) name = a;
        else {
            std::fprintf(stderr, "usage: parish-template-build <name> "
                                 "[--source=shipped|user] "
                                 "[--templates-dir=...] [--state-dir=...] "
                                 "[--base-cache=...]\n");
            return 2;
        }
    }
    if (name.empty()) { /* same usage; return 2 */ }

    std::string source_dir;
    if      (source == "shipped") source_dir = "/usr/local/share/parishbsd/templates";
    else if (source == "user")    source_dir = "/var/db/parishbsd/templates";
    else { std::fprintf(stderr, "parish: --source must be 'shipped' or 'user'\n"); return 2; }
    if (!templates_override.empty()) source_dir = templates_override;

    std::string state_dir = "/var/db/parishbsd/templates";
    if (!state_override.empty()) state_dir = state_override;

    Ops ops;
    std::string base_cache = base_cache_override.empty()
                             ? get_base_cache_path(ops)
                             : base_cache_override;
    if (base_cache.empty()) {
        std::fprintf(stderr, "parish: cannot determine base cache path "
                             "(freebsd-version or uname -m failed)\n");
        return 1;
    }

    BuildResult r = build_template(name, source_dir, state_dir, base_cache,
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