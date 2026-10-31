#include "template_build.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace parish::tbuild;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: parish-template-build <template-name>\n");
        return 2;
    }
    std::string name = argv[1];

    const std::string templates_dir = "/usr/local/share/parishbsd/templates";
    const std::string clusters_tsv = "/usr/local/libexec/parishbsd/templates/clusters.tsv";
    const std::string log_dir = "/var/log/parishbsd";
    const std::string run_dir = "/var/run/parishbsd";

    Ops ops;
    BuildResult r = build_template(name, templates_dir, clusters_tsv,
                                   log_dir, run_dir, ops);
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