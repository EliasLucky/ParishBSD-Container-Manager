#ifndef PARISH_TEMPLATE_BUILD_H
#define PARISH_TEMPLATE_BUILD_H

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace parish {
namespace tbuild {

enum class Outcome { Ok, Failed, Skipped };

// Parallel to Guardian containment.cpp's StepResult.
struct StepResult {
    std::string name;
    Outcome outcome = Outcome::Ok;
    std::string detail;   // human-readable; empty on Ok with nothing notable
};

struct BuildResult {
    std::vector<StepResult> steps;
    bool succeeded = false;   // true iff all required steps Ok and state=ready written
    std::string log_path;     // /var/log/parishbsd/template-build-<name>-<ts>.log
};

// Ops seam. Parallel to Guardian containment.cpp::Ops.
// Real implementations from Ops::Ops(). Test code overrides individual members.
struct Ops {
    // Shell-free command execution. Returns exit code (0 = success).
    std::function<int(const std::vector<std::string>&)> run_cmd;
    // Capture stdout as a string. Returns exit code.
    std::function<int(const std::vector<std::string>&, std::string&)> run_cmd_capture;
    // Capture stderr as a string. Returns exit code.
    std::function<int(const std::vector<std::string>&, std::string&)> run_cmd_capture_err;

    // ZFS-specific (thin wrappers; all through run_cmd)
    std::function<bool(const std::string&)> zfs_dataset_exists;
    std::function<bool(const std::string&)> zfs_snapshot_exists;
    std::function<bool(const std::string&, const std::string&)> zfs_clone;
    std::function<bool(const std::string&, const std::string&)> zfs_snapshot;
    std::function<bool(const std::string&, const std::string&)> zfs_rename;
    std::function<bool(const std::string&)> zfs_destroy_r;
    std::function<bool(const std::string&, unsigned long long&)> zfs_get_used_mb;

    // Provision hook dispatch: runs provision.d/<hook>.sh <path>.
    std::function<bool(const std::string& hook, const std::string& path)> run_provision_hook;

    // Filesystem checks (via stat(2), no subprocess)
    std::function<bool(const std::string&)> path_exists;

    Ops();   // initializes to real implementations
};

// The full build flow.
// - Reads template.conf from <templates_dir>/<name>/template.conf
// - Writes state= to the same file atomically
// - Logs to /var/log/parishbsd/template-build-<name>-<ts>.log
// - Writes pid file at /var/run/parishbsd/template-build-<name>.pid
// - On success: staging dataset renamed to final name, state=ready written
// - On failure: staging dataset destroyed, state=failed written
BuildResult build_template(const std::string& name,
                           const std::string& templates_dir,
                           const std::string& clusters_tsv,
                           const std::string& log_dir,
                           const std::string& run_dir,
                           Ops& ops);

}  // namespace tbuild
}  // namespace parish

#endif