#ifndef PARISH_TEMPLATE_BUILD_H
#define PARISH_TEMPLATE_BUILD_H

#include <functional>
#include <string>
#include <vector>

namespace parish {
namespace tbuild {

enum class Outcome { Ok, Failed, Skipped };

struct StepResult {
    std::string name;
    Outcome outcome = Outcome::Ok;
    std::string detail;
};

struct BuildResult {
    std::vector<StepResult> steps;
    bool succeeded = false;
    std::string log_path;
};

struct Ops {
    // Unlogged command (short/bounded). Silent; exit code only.
    std::function<int(const std::vector<std::string>&)> run_cmd;
    // Capture stdout. Returns exit code (or -2 on cancel).
    std::function<int(const std::vector<std::string>&, std::string&)> run_cmd_capture;
    // Logged command. Merges child stdout+stderr; tees to log_fd and real stderr.
    // log_fd is by-reference so tee failure can disable logging for the rest of
    // the build. Returns exit code (0 = success, -2 = cancelled).
    std::function<int(const std::vector<std::string>&, int&)> run_cmd_logged;

    std::function<bool(const std::string&)> zfs_dataset_exists;
    std::function<bool(const std::string&)> zfs_snapshot_exists;
    std::function<bool(const std::string&, const std::string&)> zfs_clone;
    std::function<bool(const std::string&, const std::string&)> zfs_snapshot;
    std::function<bool(const std::string&, const std::string&)> zfs_rename;
    std::function<bool(const std::string&)> zfs_destroy_r;
    std::function<bool(const std::string&, unsigned long long&)> zfs_get_used_mb;

    std::function<bool(const std::string& hook, const std::string& path)> run_provision_hook;
    std::function<bool(const std::string&)> path_exists;

    Ops();
};

BuildResult build_template(
    const std::string& name,
    const std::string& source_templates_dir,
    const std::string& state_templates_dir,
    const std::string& cache_dir,
    const std::string& version,
    const std::string& arch,
    const std::string& base_cache_dataset,
    const std::string& clusters_tsv,
    const std::string& log_dir,
    const std::string& run_dir,
    Ops& ops);

}  // namespace tbuild
}  // namespace parish

#endif