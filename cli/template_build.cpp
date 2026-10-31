#include "template_build.h"
#include "clusters.h"
#include "state_file.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace parish {
namespace tbuild {

namespace {

volatile std::sig_atomic_t g_cancel_requested = 0;

void on_sigterm(int) { g_cancel_requested = 1; }

// ---------------------------------------------------------------------
// Command execution
// ---------------------------------------------------------------------

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

// Bounded kill-and-reap for cancel path. Returns -2 (cancelled).
static int handle_cancel_kill(pid_t pid, int& wstatus) {
    ::kill(pid, SIGTERM);
    bool reaped = false;
    for (int i = 0; i < 50; ++i) {
        pid_t r = ::waitpid(pid, &wstatus, WNOHANG);
        if (r == pid) { reaped = true; break; }
        if (r < 0 && errno != EINTR) { reaped = true; break; }
        struct timespec ts = {0, 100000000};
        ::nanosleep(&ts, nullptr);
    }
    if (!reaped) {
        ::kill(pid, SIGKILL);
        // Known limitation: if child is in uninterruptible kernel sleep
        // (dead NFS/USB mount), this waitpid blocks forever.
        ::waitpid(pid, &wstatus, 0);
    }
    return -2;
}

int run_cmd_capture_impl(const std::vector<std::string>& argv, std::string& out) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { ::close(pipefd[0]); ::close(pipefd[1]); return -1; }
    if (pid == 0) {
        ::close(pipefd[0]);
        if (dup2(pipefd[1], 1) < 0) _exit(127);
        ::close(pipefd[1]);
        std::vector<char*> c;
        for (const auto& a : argv) c.push_back(const_cast<char*>(a.c_str()));
        c.push_back(nullptr);
        execvp(c[0], c.data());
        _exit(127);
    }
    ::close(pipefd[1]);
    char buf[4096];
    out.clear();
    for (;;) {
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) { out.append(buf, (size_t)n); continue; }
        if (n == 0) break;
        if (errno == EINTR) {
            if (g_cancel_requested) {
                ::close(pipefd[0]);
                int st = 0;
                return handle_cancel_kill(pid, st);
            }
            continue;
        }
        break;
    }
    ::close(pipefd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// Tee helper: writes to log, warns once on failure, disables log_fd.
static void tee_to_log(int& log_fd, const char* buf, size_t n, bool& warned) {
    if (log_fd < 0) return;
    size_t off = 0;
    while (off < n) {
        ssize_t w = ::write(log_fd, buf + off, n - off);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (!warned) {
            std::fprintf(stderr,
                "parish: warning: log write failed (%s); continuing without log\n",
                std::strerror(errno));
            warned = true;
        }
        ::close(log_fd);
        log_fd = -1;
        return;
    }
}

int run_cmd_logged_impl(const std::vector<std::string>& argv, int& log_fd) {
    if (argv.empty()) return -1;
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { ::close(pipefd[0]); ::close(pipefd[1]); return -1; }
    if (pid == 0) {
        ::close(pipefd[0]);
        if (dup2(pipefd[1], 1) < 0) _exit(127);
        if (dup2(pipefd[1], 2) < 0) _exit(127);
        ::close(pipefd[1]);
        std::vector<char*> c;
        for (const auto& a : argv) c.push_back(const_cast<char*>(a.c_str()));
        c.push_back(nullptr);
        execvp(c[0], c.data());
        _exit(127);
    }
    ::close(pipefd[1]);
    bool warned = false;
    char buf[4096];
    for (;;) {
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
            tee_to_log(log_fd, buf, (size_t)n, warned);
            (void)::write(2, buf, (size_t)n);
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) {
            if (g_cancel_requested) {
                ::close(pipefd[0]);
                int st = 0;
                return handle_cancel_kill(pid, st);
            }
            continue;
        }
        break;
    }
    ::close(pipefd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// ---------------------------------------------------------------------
// ZFS / filesystem wrappers
// ---------------------------------------------------------------------

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

std::string now_ts() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    gmtime_r(&t, &tmv);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmv);
    return buf;
}

// defensive name validation (mirrors validate_jail_name).
bool validate_name(const std::string& n) {
    if (n.empty() || n.size() > 63) return false;
    if (n[0] == '-' || n.back() == '-') return false;
    if (n.find("--") != std::string::npos) return false;
    for (char c : n) {
        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

bool mkdir_p(const std::string& path, mode_t mode) {
    if (path.empty()) return false;
    std::string cur;
    for (char c : path) {
        cur.push_back(c);
        if (c == '/' && cur.size() > 1) {
            if (::mkdir(cur.c_str(), mode) != 0 && errno != EEXIST) return false;
        }
    }
    if (!cur.empty() && cur != "/") {
        if (::mkdir(cur.c_str(), mode) != 0 && errno != EEXIST) return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------
// Ops
// ---------------------------------------------------------------------

Ops::Ops() {
    run_cmd             = run_cmd_impl;
    run_cmd_capture     = run_cmd_capture_impl;
    run_cmd_logged      = run_cmd_logged_impl;
    zfs_dataset_exists  = zfs_dataset_exists_impl;
    zfs_snapshot_exists = zfs_snapshot_exists_impl;
    zfs_clone           = zfs_clone_impl;
    zfs_snapshot        = zfs_snapshot_impl;
    zfs_rename          = zfs_rename_impl;
    zfs_destroy_r       = zfs_destroy_r_impl;
    zfs_get_used_mb     = zfs_get_used_mb_impl;
    run_provision_hook  = run_provision_hook_impl;
    path_exists         = path_exists_impl;
}

// ---------------------------------------------------------------------
// Base cache
// ---------------------------------------------------------------------

namespace {

enum class CacheResult {
    Exists, Constructed, MissingTxz, TxzCorrupt, PartialDataset, Other, Cancelled,
};

struct CacheOutcome {
    std::string dataset;
    CacheResult result;
    std::string detail;
};

static CacheOutcome ensure_base_cache(
    const std::string& version, const std::string& arch,
    const std::string& cache_dir, const std::string& log_path,
    Ops& ops, int& log_fd)
{
    const std::string final_ds =
        "zroot/parishbsd/templates/base-" + version + "-" + arch;

    if (ops.zfs_dataset_exists(final_ds) &&
        ops.zfs_snapshot_exists(final_ds + "@extracted")) {
        return {final_ds, CacheResult::Exists, ""};
    }
    if (ops.zfs_dataset_exists(final_ds)) {
        return {"", CacheResult::PartialDataset,
            "base cache dataset '" + final_ds + "' exists but '@extracted' "
            "snapshot is missing (partial or interrupted construction). "
            "Check 'zfs list -t snapshot zroot/parishbsd/templates/base-*'. "
            "If the dataset is incomplete: 'zfs destroy -r " + final_ds + "' "
            "and re-run this build."};
    }
    const std::string txz = cache_dir + "/base-" + version + "-" + arch + ".txz";
    if (!ops.path_exists(txz)) {
        return {"", CacheResult::MissingTxz,
            "base archive '" + txz + "' not found. "
            "Run 25-setup-templates.sh to download it for host "
            + version + "-" + arch + "."};
    }
    int rc = ops.run_cmd_logged({"/sbin/zfs", "create", final_ds}, log_fd);
    if (rc == -2) return {"", CacheResult::Cancelled, ""};
    if (rc != 0) {
        return {"", CacheResult::Other,
            "zfs create '" + final_ds + "' failed. See log: " + log_path};
    }
    std::string mp;
    if (ops.run_cmd_capture(
            {"/sbin/zfs", "get", "-H", "-o", "value", "mountpoint", final_ds},
            mp) != 0) {
        ops.zfs_destroy_r(final_ds);
        return {"", CacheResult::Other,
            "cannot read mountpoint of '" + final_ds + "'. See log: " + log_path};
    }
    while (!mp.empty() && (mp.back()=='\n' || mp.back()=='\r')) mp.pop_back();
    if (mp.empty() || mp[0] != '/') {
        ops.zfs_destroy_r(final_ds);
        return {"", CacheResult::Other,
            "unexpected mountpoint for '" + final_ds + "': '" + mp + "'"};
    }
    rc = ops.run_cmd_logged(
        {"/usr/bin/tar", "-xJf", txz, "-C", mp, "--strip-components=1"},
        log_fd);
    if (rc == -2) {
        ops.zfs_destroy_r(final_ds);
        return {"", CacheResult::Cancelled, ""};
    }
    if (rc != 0) {
        ops.zfs_destroy_r(final_ds);
        return {"", CacheResult::TxzCorrupt,
            "extraction of '" + txz + "' failed; the .txz may be corrupt. "
            "Re-download via 25-setup-templates.sh, or delete the .txz and "
            "retry. See log: " + log_path};
    }
    if (!ops.zfs_snapshot(final_ds, "extracted")) {
        ops.zfs_destroy_r(final_ds);
        return {"", CacheResult::Other,
            "cannot snapshot '" + final_ds + "@extracted'. See log: " + log_path};
    }
    return {final_ds, CacheResult::Constructed, ""};
}

}  // namespace

// ---------------------------------------------------------------------
// build_template
// ---------------------------------------------------------------------

static StepResult step(const std::string& name) {
    StepResult r; r.name = name; r.outcome = Outcome::Ok; return r;
}

static void fail_step(StepResult& r, const std::string& detail) {
    r.outcome = Outcome::Failed;
    r.detail = detail;
}

BuildResult build_template(
    const std::string& name,
    const std::string& source_templates_dir,
    const std::string& state_templates_dir,
    const std::string& cache_dir,
    const std::string& version,
    const std::string& arch,
    const std::string& base_cache_dataset_in,
    const std::string& clusters_tsv,
    const std::string& log_dir,
    const std::string& run_dir,
    Ops& ops)
{
    BuildResult result;

    // --- Step 0: validate name ---
    if (!validate_name(name)) {
        auto r = step("validate name");
        fail_step(r, "invalid template name '" + name + "'");
        result.steps.push_back(r);
        return result;
    }

    // --- Step 1: install SIGTERM handler ---
    {
        struct sigaction sa{};
        sa.sa_handler = on_sigterm;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;   // no SA_RESTART: EINTR needed
        if (::sigaction(SIGTERM, &sa, nullptr) != 0) {
            auto r = step("install SIGTERM handler");
            fail_step(r, std::string("sigaction: ") + std::strerror(errno));
            result.steps.push_back(r);
            return result;
        }
        result.steps.push_back(step("install SIGTERM handler"));
    }

    // Paths
    const std::string source_conf = source_templates_dir + "/" + name + "/template.conf";
    const std::string state_conf  = state_templates_dir  + "/" + name + "/template.conf";
    const std::string staging_ds  = "zroot/parishbsd/templates/" + name + "-building";
    const std::string final_ds    = "zroot/parishbsd/templates/" + name;
    const std::string ts          = now_ts();
    const std::string base_snap   = "base-" + ts;
    const std::string log_path    = log_dir + "/template-build-" + name + "-" + ts + ".log";
    const std::string pid_path    = run_dir + "/template-build-" + name + ".pid";
    result.log_path = log_path;

    // Prepare log directory
    if (!mkdir_p(log_dir, 0755)) {
        auto r = step("prepare log dir");
        fail_step(r, "cannot create '" + log_dir + "': " + std::strerror(errno));
        result.steps.push_back(r);
        return result;
    }

    int log_fd = ::open(log_path.c_str(), O_WRONLY|O_CREAT|O_APPEND, 0600);
    if (log_fd < 0) {
        auto r = step("open log");
        fail_step(r, "cannot open '" + log_path + "': " + std::strerror(errno));
        result.steps.push_back(r);
        return result;
    }

    auto mark_step = [&](const std::string& sn) -> StepResult& {
        if (log_fd >= 0) {
            std::string line = "[step] " + sn + "\n";
            (void)::write(log_fd, line.data(), line.size());
        }
        result.steps.push_back(step(sn));
        return result.steps.back();
    };

    auto log = [&](const std::string& s) {
        if (log_fd >= 0) {
            std::string l = s + "\n";
            (void)::write(log_fd, l.data(), l.size());
        }
    };

    bool pid_acquired = false;
    bool state_written = false;

    auto check_cancel = [&]() -> bool {
        if (!g_cancel_requested) return false;
        auto& r = mark_step("cancelled");
        fail_step(r, "cancelled by SIGTERM");
        return true;
    };

    // --- Step 2: pre-pid cancel ---
    if (check_cancel()) goto fail;

    // --- Step 3: acquire pid ---
    {
        auto& r = mark_step("acquire build lock");
        int fd = ::open(pid_path.c_str(), O_WRONLY|O_CREAT|O_EXCL, 0600);
        if (fd < 0) {
            if (errno == EEXIST) {
                fail_step(r, "a build for '" + name + "' is already running "
                             "(pid file '" + pid_path + "' exists). "
                             "If that build is not running, run "
                             "'parishctl template kill " + name + "' to clear "
                             "the stale lock.");
            } else {
                fail_step(r, std::string("cannot create pid file: ") +
                             std::strerror(errno));
            }
            goto fail;
        }
        std::string pid_str = std::to_string(::getpid()) + "\n";
        (void)::write(fd, pid_str.data(), pid_str.size());
        ::close(fd);
        pid_acquired = true;
    }

    if (check_cancel()) goto fail;

    // --- Step 4: name availability ---
    {
        auto& r = mark_step("check name availability");
        if (ops.zfs_dataset_exists(final_ds)) {
            fail_step(r, "template '" + name + "' already exists");
            goto fail;
        }
        if (ops.zfs_dataset_exists(staging_ds)) {
            fail_step(r, "staging dataset '" + staging_ds + "' exists from a "
                         "prior failed build; run 'parishctl template scrub'");
            goto fail;
        }
    }

    if (check_cancel()) goto fail;

    // --- Step 5: ensure state exists + load from state ---
    std::map<std::string, std::string> conf;
    std::string err;
    {
        auto& r = mark_step("load template.conf");
        if (!ops.path_exists(state_conf)) {
            if (!ops.path_exists(source_conf)) {
                fail_step(r, "template '" + name + "' not found at either '"
                             + source_conf + "' or '" + state_conf + "'");
                goto fail;
            }
            if (!mkdir_p(state_templates_dir + "/" + name, 0755)) {
                fail_step(r, "cannot create state dir for '" + name + "': "
                             + std::strerror(errno));
                goto fail;
            }
            if (ops.run_cmd({"/bin/cp", source_conf, state_conf}) != 0) {
                fail_step(r, "cannot copy template.conf to state dir");
                goto fail;
            }
            if (ops.run_cmd({"/bin/chmod", "0600", state_conf}) != 0) {
                fail_step(r, "cannot chmod state template.conf");
                goto fail;
            }
        }
        if (!parse_state_file(state_conf, conf, err)) {
            fail_step(r, err);
            goto fail;
        }
    }

    if (check_cancel()) goto fail;

    // --- Step 6: write state=building ---
    {
        auto& r = mark_step("write state=building");
        conf["state"] = "building";
        if (!write_state_file_atomic(state_conf, conf, err)) {
            fail_step(r, err);
            goto fail;
        }
        state_written = true;
    }

    if (check_cancel()) goto fail;

    // --- Step 7: base cache ---
    std::string base_cache_dataset = base_cache_dataset_in;
    {
        auto& r = mark_step("base cache check");
        if (base_cache_dataset.empty()) {
            CacheOutcome co = ensure_base_cache(version, arch, cache_dir,
                                                log_path, ops, log_fd);
            if (co.result == CacheResult::Cancelled) {
                fail_step(r, "cancelled");
                goto fail;
            }
            if (co.dataset.empty()) {
                fail_step(r, co.detail);
                goto fail;
            }
            base_cache_dataset = co.dataset;
        } else {
            if (!ops.zfs_snapshot_exists(base_cache_dataset + "@extracted")) {
                fail_step(r, "base cache '" + base_cache_dataset +
                             "@extracted' missing");
                goto fail;
            }
        }
    }

    if (check_cancel()) goto fail;

    // --- Step 8: clone staging ---
    {
        auto& r = mark_step("clone staging dataset");
        if (!ops.zfs_clone(base_cache_dataset + "@extracted", staging_ds)) {
            fail_step(r, "zfs clone failed");
            goto fail;
        }
    }

    if (check_cancel()) goto fail;

    // --- Step 9: pkg update ---
    {
        auto& r = mark_step("pkg update");
        int ret = ops.run_cmd_logged(
            {"/usr/local/sbin/pkg", "-r", staging_ds, "update"}, log_fd);
        if (ret == -2) { fail_step(r, "cancelled"); goto fail; }
        if (ret != 0)  { fail_step(r, "pkg update failed"); goto fail; }
    }

    if (check_cancel()) goto fail;

    // --- Step 10: resolve clusters ---
    std::vector<Cluster> resolved;
    {
        auto& r = mark_step("resolve components");
        ClustersDb db;
        if (!parse_clusters_tsv(clusters_tsv, db, err)) {
            fail_step(r, err);
            goto fail;
        }
        std::vector<std::string> requested;
        auto push_csv = [&](const std::string& s, std::vector<std::string>& v) {
            std::string cur;
            for (char c : s) {
                if (c == ',') { if (!cur.empty()) v.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            if (!cur.empty()) v.push_back(cur);
        };
        auto it = conf.find("clusters");
        if (it != conf.end()) push_csv(it->second, requested);
        it = conf.find("applications");
        if (it != conf.end()) push_csv(it->second, requested);

        if (!resolve_clusters(requested, db, resolved, err)) {
            fail_step(r, err);
            goto fail;
        }
    }

    // --- Step 11: install clusters in order ---
    for (const auto& c : resolved) {
        if (check_cancel()) goto fail;
        std::string sn = c.no_pkg_install ? ("provision " + c.name)
                                          : ("install " + c.name);
        auto& r = mark_step(sn);
        if (!c.no_pkg_install && !c.packages.empty()) {
            std::vector<std::string> argv =
                {"/usr/local/sbin/pkg", "-r", staging_ds, "install", "-y"};
            for (const auto& p : c.packages) argv.push_back(p);
            int ret = ops.run_cmd_logged(argv, log_fd);
            if (ret == -2) { fail_step(r, "cancelled"); goto fail; }
            if (ret != 0)  { fail_step(r, "pkg install failed"); goto fail; }
        }
        for (const auto& hook : c.provisions) {
            if (!ops.run_provision_hook(hook, staging_ds)) {
                fail_step(r, "provision hook failed: " + hook);
                goto fail;
            }
        }
    }

    // --- Step 12: extra packages ---
    {
        auto it = conf.find("extra_packages");
        if (it != conf.end() && !it->second.empty()) {
            if (check_cancel()) goto fail;
            auto& r = mark_step("install extra packages");
            std::vector<std::string> argv =
                {"/usr/local/sbin/pkg", "-r", staging_ds, "install", "-y"};
            std::string cur;
            for (char c : it->second) {
                if (c == ',') { if (!cur.empty()) argv.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            if (!cur.empty()) argv.push_back(cur);
            if (argv.size() > 5) {
                int ret = ops.run_cmd_logged(argv, log_fd);
                if (ret == -2) { fail_step(r, "cancelled"); goto fail; }
                if (ret != 0)  { fail_step(r, "pkg install (extra) failed"); goto fail; }
            }
        }
    }

    if (check_cancel()) goto fail;

    // --- Step 13: snapshot staging ---
    {
        auto& r = mark_step("snapshot staging");
        if (!ops.zfs_snapshot(staging_ds, base_snap)) {
            fail_step(r, "zfs snapshot failed");
            goto fail;
        }
    }

    if (check_cancel()) goto fail;

    // --- Step 14: rename staging to final ---
    {
        auto& r = mark_step("rename staging to final");
        if (!ops.zfs_rename(staging_ds, final_ds)) {
            fail_step(r, "zfs rename failed");
            goto fail;
        }
    }

    // --- Step 15: measure size ---
    {
        auto& r = mark_step("measure size");
        unsigned long long mb = 0;
        if (ops.zfs_get_used_mb(final_ds, mb)) {
            conf["build_size_mb"] = std::to_string(mb);
        }
    }

    // --- Step 16: write state=ready ---
    {
        auto& r = mark_step("write state=ready");
        conf["state"] = "ready";
        conf["current_base"] = "@" + base_snap;
        conf["updated"] = ts;
        if (!write_state_file_atomic(state_conf, conf, err)) {
            fail_step(r, err);
            goto fail;
        }
    }

    result.succeeded = true;
    if (log_fd >= 0) ::close(log_fd);
    ::unlink(pid_path.c_str());
    return result;

fail:
    log(g_cancel_requested ? "BUILD CANCELLED" : "BUILD FAILED");
    if (ops.zfs_dataset_exists(staging_ds)) {
        ops.zfs_destroy_r(staging_ds);
    }
    if (state_written) {
        conf["state"] = "failed";
        (void)write_state_file_atomic(state_conf, conf, err);
    }
    if (pid_acquired) {
        ::unlink(pid_path.c_str());
    }
    if (log_fd >= 0) {
        ::close(log_fd);
    }
    return result;
}

}  // namespace tbuild
}  // namespace parish