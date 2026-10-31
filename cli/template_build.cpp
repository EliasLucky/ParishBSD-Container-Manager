#include "template_build.h"
#include "clusters.h"
#include "state_file.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace parish {
namespace tbuild {

namespace {

// Fork + execvp. No shell. Wait with EINTR loop. Returns exit code.
int run_cmd_impl(const std::vector<std::string>& argv) {
    if (argv.empty()) return -1;
    std::vector<char*> c;
    for (const auto& a : argv) c.push_back(const_cast<char*>(a.c_str()));
    c.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(c[0], c.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// Same, but captures stdout to a string.
int run_cmd_capture_impl(const std::vector<std::string>& argv, std::string& out) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], 1);
        close(pipefd[1]);
        std::vector<char*> c;
        for (const auto& a : argv) c.push_back(const_cast<char*>(a.c_str()));
        c.push_back(nullptr);
        execvp(c[0], c.data());
        _exit(127);
    }
    close(pipefd[1]);
    char buf[4096];
    ssize_t n;
    out.clear();
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) out.append(buf, (size_t)n);
    close(pipefd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

bool zfs_dataset_exists_impl(const std::string& name) {
    return run_cmd_impl({"/sbin/zfs", "list", "-H", "-o", "name", name}) == 0;
}

bool zfs_snapshot_exists_impl(const std::string& name) {
    return run_cmd_impl({"/sbin/zfs", "list", "-H", "-t", "snapshot", "-o",
                         "name", name}) == 0;
}

bool zfs_clone_impl(const std::string& src, const std::string& dst) {
    return run_cmd_impl({"/sbin/zfs", "clone", src, dst}) == 0;
}

bool zfs_snapshot_impl(const std::string& ds, const std::string& snap) {
    return run_cmd_impl({"/sbin/zfs", "snapshot", ds + "@" + snap}) == 0;
}

bool zfs_rename_impl(const std::string& from, const std::string& to) {
    return run_cmd_impl({"/sbin/zfs", "rename", from, to}) == 0;
}

bool zfs_destroy_r_impl(const std::string& name) {
    return run_cmd_impl({"/sbin/zfs", "destroy", "-r", name}) == 0;
}

bool zfs_get_used_mb_impl(const std::string& name, unsigned long long& mb) {
    std::string out;
    if (run_cmd_capture_impl({"/sbin/zfs", "get", "-H", "-o", "value", "used",
                              name}, out) != 0) return false;
    if (out.empty()) return false;
    errno = 0;
    unsigned long long bytes = std::strtoull(out.c_str(), nullptr, 10);
    if (errno != 0) return false;
    mb = bytes / (1024ULL * 1024ULL);
    return true;
}

bool run_provision_hook_impl(const std::string& hook, const std::string& path) {
    std::string script = "/usr/local/libexec/parishbsd/templates/provision.d/"
                         + hook + ".sh";
    return run_cmd_impl({"/bin/sh", script, path}) == 0;
}

bool path_exists_impl(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

// Timestamp for snapshots and logs.
std::string now_ts() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    gmtime_r(&t, &tmv);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmv);
    return buf;
}

}  // namespace

Ops::Ops() {
    run_cmd = run_cmd_impl;
    run_cmd_capture = run_cmd_capture_impl;
    run_cmd_capture_err = [](const std::vector<std::string>&, std::string&){ return -1; };
    zfs_dataset_exists = zfs_dataset_exists_impl;
    zfs_snapshot_exists = zfs_snapshot_exists_impl;
    zfs_clone = zfs_clone_impl;
    zfs_snapshot = zfs_snapshot_impl;
    zfs_rename = zfs_rename_impl;
    zfs_destroy_r = zfs_destroy_r_impl;
    zfs_get_used_mb = zfs_get_used_mb_impl;
    run_provision_hook = run_provision_hook_impl;
    path_exists = path_exists_impl;
}

// --- Orchestration ---

static StepResult step(const std::string& name) {
    StepResult r; r.name = name; r.outcome = Outcome::Ok; return r;
}

static void fail_step(StepResult& r, const std::string& detail) {
    r.outcome = Outcome::Failed;
    r.detail = detail;
}

BuildResult build_template(const std::string& name,
                           const std::string& templates_dir,
                           const std::string& clusters_tsv,
                           const std::string& log_dir,
                           const std::string& run_dir,
                           Ops& ops)
{
    BuildResult result;
    const std::string conf_path = templates_dir + "/" + name + "/template.conf";
    const std::string staging = "zroot/parishbsd/templates/" + name + "-building";
    const std::string final_ds = "zroot/parishbsd/templates/" + name;
    const std::string ts = now_ts();
    const std::string base_snap = "base-" + ts;
    const std::string log_path = log_dir + "/template-build-" + name + "-" + ts + ".log";
    const std::string pid_path = run_dir + "/template-build-" + name + ".pid";
    result.log_path = log_path;

    auto log = [&](const std::string& s) {
        int fd = ::open(log_path.c_str(), O_WRONLY|O_CREAT|O_APPEND, 0600);
        if (fd >= 0) { std::string l = s + "\n"; (void)!::write(fd, l.data(), l.size()); ::close(fd); }
    };

    // 0. Load template.conf.
    auto r_load = step("load template.conf");
    std::map<std::string, std::string> conf;
    std::string err;
    if (!parse_state_file(conf_path, conf, err)) {
        fail_step(r_load, err); result.steps.push_back(r_load);
        log("FAIL: " + err);
        return result;
    }
    result.steps.push_back(r_load);

    // Refuse if dataset exists under either name.
    auto r_pre = step("check name availability");
    if (ops.zfs_dataset_exists(final_ds)) {
        fail_step(r_pre, "template '" + name + "' already exists"); goto fail;
    }
    if (ops.zfs_dataset_exists(staging)) {
        fail_step(r_pre, "staging dataset for '" + name + "' exists from a prior failed build; run 'parishctl template scrub'"); goto fail;
    }
    result.steps.push_back(r_pre);

    // 1. Write state=building.
    {
        auto r = step("write state=building");
        conf["state"] = "building";
        if (!write_state_file_atomic(conf_path, conf, err)) { fail_step(r, err); result.steps.push_back(r); goto fail; }
        result.steps.push_back(r);
    }

    // Write pid file.
    {
        int fd = ::open(pid_path.c_str(), O_WRONLY|O_CREAT|O_EXCL, 0600);
        if (fd >= 0) {
            std::string s = std::to_string(::getpid()) + "\n";
            (void)!::write(fd, s.data(), s.size());
            ::close(fd);
        }
    }

    // 2. Verify base cache; fetch + extract if missing.
    {
        auto r = step("base cache check");
        const std::string base_cache = "zroot/parishbsd/templates/base-cache";
        // Platform-fact: base cache key is version+arch.
        // For, assume the cache is present (built by 25-setup-templates).
        // Full fetch-and-extract logic lands in if the cache is absent.
        if (!ops.zfs_dataset_exists(base_cache)) {
            fail_step(r, "base cache dataset " + base_cache + " not found; run 25-setup-templates first");
            result.steps.push_back(r); goto fail;
        }
        result.steps.push_back(r);
    }

    // 3. Clone staging from cache.
    {
        auto r = step("clone staging dataset");
        if (!ops.zfs_clone("zroot/parishbsd/templates/base-cache@extracted", staging)) {
            fail_step(r, "zfs clone failed"); result.steps.push_back(r); goto fail;
        }
        result.steps.push_back(r);
    }

    // 4. pkg update.
    {
        auto r = step("pkg update");
        if (ops.run_cmd({"/usr/local/sbin/pkg", "-r", staging, "update"}) != 0) {
            fail_step(r, "pkg update failed"); result.steps.push_back(r); goto fail;
        }
        result.steps.push_back(r);
    }

    // 5-9. Resolve and install.
    {
        auto r = step("resolve components");
        ClustersDb db;
        if (!parse_clusters_tsv(clusters_tsv, db, err)) {
            fail_step(r, err); result.steps.push_back(r); goto fail;
        }
        std::vector<std::string> requested;
        // conf["clusters"] is comma-separated; may be empty.
        if (!conf["clusters"].empty()) {
            std::string cur;
            for (char c : conf["clusters"]) {
                if (c == ',') { if (!cur.empty()) requested.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            if (!cur.empty()) requested.push_back(cur);
        }
        // Presets from conf["applications"].
        if (!conf["applications"].empty()) {
            std::string cur;
            for (char c : conf["applications"]) {
                if (c == ',') { if (!cur.empty()) requested.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            if (!cur.empty()) requested.push_back(cur);
        }
        std::vector<Cluster> resolved;
        if (!resolve_clusters(requested, db, resolved, err)) {
            fail_step(r, err); result.steps.push_back(r); goto fail;
        }
        result.steps.push_back(r);

        for (const auto& c : resolved) {
            auto rs = step("install " + c.name);
            if (!c.no_pkg_install && !c.packages.empty()) {
                std::vector<std::string> argv = {"/usr/local/sbin/pkg", "-r", staging, "install", "-y"};
                for (const auto& p : c.packages) argv.push_back(p);
                if (ops.run_cmd(argv) != 0) { fail_step(rs, "pkg install failed"); result.steps.push_back(rs); goto fail; }
            }
            for (const auto& hook : c.provisions) {
                if (!ops.run_provision_hook(hook, staging)) {
                    fail_step(rs, "provision hook failed: " + hook);
                    result.steps.push_back(rs); goto fail;
                }
            }
            result.steps.push_back(rs);
        }
    }

    // 10. Extra packages.
    if (!conf["extra_packages"].empty()) {
        auto r = step("install extra packages");
        std::vector<std::string> argv = {"/usr/local/sbin/pkg", "-r", staging, "install", "-y"};
        std::string cur;
        for (char c : conf["extra_packages"]) {
            if (c == ',') { if (!cur.empty()) argv.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) argv.push_back(cur);
        if (argv.size() > 5 && ops.run_cmd(argv) != 0) {
            fail_step(r, "pkg install (extra) failed"); result.steps.push_back(r); goto fail;
        }
        result.steps.push_back(r);
    }

    // 11. Snapshot staging as @base-<timestamp>.
    {
        auto r = step("snapshot staging");
        if (!ops.zfs_snapshot(staging, base_snap)) {
            fail_step(r, "zfs snapshot failed"); result.steps.push_back(r); goto fail;
        }
        result.steps.push_back(r);
    }

    // 12. Rename staging to final.
    // All children have exited (each run_cmd waits). No cwd is under staging.
    {
        auto r = step("rename staging to final");
        if (!ops.zfs_rename(staging, final_ds)) {
            fail_step(r, "zfs rename failed"); result.steps.push_back(r); goto fail;
        }
        result.steps.push_back(r);
    }

    // 13. Measure size.
    {
        auto r = step("measure size");
        unsigned long long mb = 0;
        if (ops.zfs_get_used_mb(final_ds, mb)) {
            conf["build_size_mb"] = std::to_string(mb);
        }
        result.steps.push_back(r);
    }

    // 14. Write state=ready.
    {
        auto r = step("write state=ready");
        conf["state"] = "ready";
        conf["current_base"] = "@" + base_snap;
        conf["updated"] = ts;
        if (!write_state_file_atomic(conf_path, conf, err)) { fail_step(r, err); result.steps.push_back(r); goto fail; }
        result.steps.push_back(r);
    }

    result.succeeded = true;
    ::unlink(pid_path.c_str());
    return result;

fail:
    log("BUILD FAILED");
    // 15. Failure cleanup.
    if (ops.zfs_dataset_exists(staging)) {
        ops.zfs_destroy_r(staging);
    }
    conf["state"] = "failed";
    (void)write_state_file_atomic(conf_path, conf, err);
    ::unlink(pid_path.c_str());
    return result;
}

}  // namespace tbuild
}  // namespace parish