#include "../clusters.h"
#include "../state_file.h"
#include "../template_build.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace parish::tbuild;

// ---------- helpers ----------

static void write_file(const std::string& p, const std::string& body) {
    std::ofstream(p) << body;
}

static void rm_rf(const std::string& p) {
    std::string cmd = "rm -rf '" + p + "'";
    (void)::system(cmd.c_str());
}

static void mkdir_p(const std::string& p) {
    std::string cmd = "mkdir -p '" + p + "'";
    (void)::system(cmd.c_str());
}

// Test Ops that records calls and allows overriding each op's return.
struct TestOps {
    Ops ops;
    std::vector<std::string> calls;

    // Responses
    bool zfs_dataset_exists_ret = false;
    bool zfs_snapshot_exists_ret = false;
    bool zfs_clone_ret = true;
    bool zfs_snapshot_ret = true;
    bool zfs_rename_ret = true;
    bool zfs_destroy_r_ret = true;
    bool path_exists_ret = true;
    bool provision_hook_ret = true;
    int  run_cmd_ret = 0;
    int  run_cmd_logged_ret = 0;
    int  run_cmd_capture_ret = 0;
    std::string run_cmd_capture_out = "zroot/cache/mp\n";

    TestOps() {
        ops.zfs_dataset_exists = [this](const std::string& n) {
            calls.push_back("zfs_dataset_exists:" + n);
            return zfs_dataset_exists_ret;
        };
        ops.zfs_snapshot_exists = [this](const std::string& n) {
            calls.push_back("zfs_snapshot_exists:" + n);
            return zfs_snapshot_exists_ret;
        };
        ops.zfs_clone = [this](const std::string& s, const std::string& d) {
            calls.push_back("zfs_clone:" + s + "->" + d);
            return zfs_clone_ret;
        };
        ops.zfs_snapshot = [this](const std::string& d, const std::string& s) {
            calls.push_back("zfs_snapshot:" + d + "@" + s);
            return zfs_snapshot_ret;
        };
        ops.zfs_rename = [this](const std::string& f, const std::string& t) {
            calls.push_back("zfs_rename:" + f + "->" + t);
            return zfs_rename_ret;
        };
        ops.zfs_destroy_r = [this](const std::string& n) {
            calls.push_back("zfs_destroy_r:" + n);
            return zfs_destroy_r_ret;
        };
        ops.zfs_get_used_mb = [this](const std::string&, unsigned long long& mb) {
            calls.push_back("zfs_get_used_mb");
            mb = 100;
            return true;
        };
        ops.run_cmd = [this](const std::vector<std::string>& a) {
            calls.push_back("run_cmd:" + (a.empty() ? "" : a[0]));
            return run_cmd_ret;
        };
        ops.run_cmd_capture = [this](const std::vector<std::string>& a, std::string& o) {
            calls.push_back("run_cmd_capture:" + (a.empty() ? "" : a[0]));
            o = run_cmd_capture_out;
            return run_cmd_capture_ret;
        };
        ops.run_cmd_logged = [this](const std::vector<std::string>& a, int&) {
            calls.push_back("run_cmd_logged:" + (a.empty() ? "" : a[0]));
            return run_cmd_logged_ret;
        };
        ops.run_provision_hook = [this](const std::string& h, const std::string&) {
            calls.push_back("run_provision_hook:" + h);
            return provision_hook_ret;
        };
        ops.path_exists = [this](const std::string& p) {
            calls.push_back("path_exists:" + p);
            return path_exists_ret;
        };
    }
};

// ---------- cluster parser tests ----------

static void test_parser_rejects_space_separated() {
    std::string path = "/tmp/clusters.tsv.test";
    write_file(path, "T-core required - -\n");
    ClustersDb db; std::string err;
    bool ok = parse_clusters_tsv(path, db, err);
    std::remove(path.c_str());
    assert(!ok && "space-separated must be rejected");
}

static void test_parser_rejects_unknown_requires() {
    std::string path = "/tmp/clusters.tsv.test";
    write_file(path,
        "T-core\trequired\t-\t-\n"
        "T-xpra\trequired\txpra\trequires:T-fonts-min\n");
    ClustersDb db; std::string err;
    bool ok = parse_clusters_tsv(path, db, err);
    std::remove(path.c_str());
    assert(!ok && "unknown requires target must be rejected");
}

static void test_parser_accepts_valid() {
    std::string path = "/tmp/clusters.tsv.test";
    write_file(path,
        "T-appuser\trequired\t-\tprovision:app-user\n"
        "T-fonts-min\trequired\tfontconfig\t-\n"
        "T-xpra\trequired\txpra\trequires:T-fonts-min\n"
        "P-browser\tpreset\tfirefox\tgroup:browser\n");
    ClustersDb db; std::string err;
    assert(parse_clusters_tsv(path, db, err));
    assert(db.by_name.size() == 4);
    std::remove(path.c_str());
}

// ---------- resolver tests ----------

static void test_resolver_puts_appuser_first() {
    std::string path = "/tmp/clusters.tsv.test";
    write_file(path,
        "T-appuser\trequired\t-\tprovision:app-user\n"
        "T-fonts-min\trequired\tfontconfig\t-\n"
        "T-xpra\trequired\txpra\trequires:T-fonts-min\n");
    ClustersDb db; std::string err;
    assert(parse_clusters_tsv(path, db, err));
    std::vector<Cluster> out;
    assert(resolve_clusters({"T-xpra"}, db, out, err));
    std::remove(path.c_str());
    assert(!out.empty() && out[0].name == "T-appuser");
    size_t fonts_idx = 0, xpra_idx = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i].name == "T-fonts-min") fonts_idx = i;
        if (out[i].name == "T-xpra")      xpra_idx = i;
    }
    assert(fonts_idx < xpra_idx && "T-fonts-min must precede T-xpra");
}

static void test_resolver_detects_cycle() {
    std::string path = "/tmp/clusters.tsv.test";
    write_file(path,
        "T-appuser\trequired\t-\tprovision:app-user\n"
        "T-a\trequired\tpa\trequires:T-b\n"
        "T-b\trequired\tpb\trequires:T-a\n");
    ClustersDb db; std::string err;
    assert(parse_clusters_tsv(path, db, err));
    std::vector<Cluster> out;
    bool ok = resolve_clusters({"T-a"}, db, out, err);
    std::remove(path.c_str());
    assert(!ok);
    assert(err.find("cycle") != std::string::npos);
}

// ---------- state file tests ----------

static void test_state_file_atomic_roundtrip() {
    std::string path = "/tmp/state.test";
    std::map<std::string, std::string> kv = {{"name","x"},{"state","building"}};
    std::string err;
    assert(write_state_file_atomic(path, kv, err));
    std::map<std::string, std::string> parsed;
    assert(parse_state_file(path, parsed, err));
    std::remove(path.c_str());
    assert(parsed["state"] == "building");
}

static void test_state_file_preserves_unknown_keys() {
    std::string path = "/tmp/state.test";
    std::map<std::string, std::string> kv = {
        {"name","x"},{"state","ready"},{"user_marker","preserved"}
    };
    std::string err;
    assert(write_state_file_atomic(path, kv, err));
    std::map<std::string, std::string> parsed;
    assert(parse_state_file(path, parsed, err));
    std::remove(path.c_str());
    assert(parsed["user_marker"] == "preserved");
}

// ---------- build_template tests (fake Ops) ----------

static std::string setup_template_conf(const std::string& dir,
                                       const std::string& name,
                                       const std::string& body)
{
    mkdir_p(dir + "/" + name);
    std::string p = dir + "/" + name + "/template.conf";
    write_file(p, body);
    return p;
}

static void test_build_refuses_invalid_name() {
    std::string dir = "/tmp/parish-tb-inv";
    rm_rf(dir); mkdir_p(dir);
    TestOps t; t.path_exists_ret = true;
    BuildResult r = build_template(
        "../etc", dir, dir, dir, "15.1-RELEASE", "amd64", "",
        dir + "/clusters.tsv", dir, dir, t.ops);
    rm_rf(dir);
    assert(!r.succeeded);
    assert(r.steps[0].name == "validate name");
}

static void test_build_refuses_staging_exists() {
    std::string dir = "/tmp/parish-tb-stage";
    rm_rf(dir); mkdir_p(dir);
    setup_template_conf(dir, "x",
        "name=x\nstate=building\nclusters=T-core\n");
    TestOps t;
    // zfs_dataset_exists: final false, staging true.
    // Simulate via switch based on called name.
    std::string stage_name = "zroot/parishbsd/templates/x-building";
    t.ops.zfs_dataset_exists = [&](const std::string& n) {
        return n == stage_name;
    };
    t.path_exists_ret = true;
    BuildResult r = build_template(
        "x", dir, dir, dir, "15.1-RELEASE", "amd64", "",
        dir + "/clusters.tsv", dir, dir, t.ops);
    rm_rf(dir);
    assert(!r.succeeded);
    // Find the name-availability step
    bool found = false;
    for (const auto& s : r.steps) {
        if (s.name == "check name availability" && s.outcome == Outcome::Failed) {
            assert(s.detail.find("prior failed build") != std::string::npos);
            found = true;
        }
    }
    assert(found && "expected 'prior failed build' at name-availability");
}

static void test_build_happy_path_fake_ops() {
    std::string dir = "/tmp/parish-tb-happy";
    rm_rf(dir); mkdir_p(dir);
    std::string ctsv = dir + "/clusters.tsv";
    write_file(ctsv,
        "T-core\trequired\t-\t-\n"
        "T-appuser\trequired\t-\tprovision:app-user\n"
        "T-fonts-min\trequired\tfontconfig\t-\n");
    setup_template_conf(dir, "y",
        "name=y\nstate=building\n"
        "clusters=T-core,T-appuser,T-fonts-min\n");

    TestOps t;
    t.zfs_dataset_exists_ret = false;      // no prior final/staging
    t.zfs_snapshot_exists_ret = true;      // base cache has @extracted
    t.zfs_clone_ret = true;
    t.zfs_snapshot_ret = true;
    t.zfs_rename_ret = true;
    t.run_cmd_ret = 0;
    t.run_cmd_logged_ret = 0;
    t.provision_hook_ret = true;
    t.path_exists_ret = true;

    BuildResult r = build_template(
        "y", dir, dir, dir, "15.1-RELEASE", "amd64",
        "zroot/parishbsd/templates/base-15.1-RELEASE-amd64",
        ctsv, dir, dir, t.ops);

    // Verify final state file
    std::map<std::string, std::string> conf;
    std::string err;
    assert(parse_state_file(dir + "/y/template.conf", conf, err));
    bool ok = r.succeeded
        && conf["state"] == "ready"
        && conf.count("current_base") == 1
        && conf.count("build_size_mb") == 1;
    rm_rf(dir);
    assert(ok && "happy path must reach state=ready");
}

static void test_build_fails_when_clone_fails() {
    std::string dir = "/tmp/parish-tb-clone";
    rm_rf(dir); mkdir_p(dir);
    std::string ctsv = dir + "/clusters.tsv";
    write_file(ctsv,
        "T-core\trequired\t-\t-\n"
        "T-appuser\trequired\t-\tprovision:app-user\n");
    setup_template_conf(dir, "c",
        "name=c\nstate=building\nclusters=T-core\n");
    TestOps t;
    t.zfs_dataset_exists_ret = false;
    t.zfs_snapshot_exists_ret = true;
    t.zfs_clone_ret = false;   // inject failure
    t.provision_hook_ret = true;
    BuildResult r = build_template(
        "c", dir, dir, dir, "15.1-RELEASE", "amd64",
        "zroot/parishbsd/templates/base-15.1-RELEASE-amd64",
        ctsv, dir, dir, t.ops);
    std::map<std::string, std::string> conf;
    std::string err;
    assert(parse_state_file(dir + "/c/template.conf", conf, err));
    bool ok = !r.succeeded
        && conf["state"] == "failed";
    rm_rf(dir);
    assert(ok);
}

static void test_build_fails_when_provision_fails() {
    std::string dir = "/tmp/parish-tb-hook";
    rm_rf(dir); mkdir_p(dir);
    std::string ctsv = dir + "/clusters.tsv";
    write_file(ctsv,
        "T-core\trequired\t-\t-\n"
        "T-appuser\trequired\t-\tprovision:app-user\n");
    setup_template_conf(dir, "h",
        "name=h\nstate=building\nclusters=T-core,T-appuser\n");
    TestOps t;
    t.zfs_dataset_exists_ret = false;
    t.zfs_snapshot_exists_ret = true;
    t.zfs_clone_ret = true;
    t.provision_hook_ret = false;
    BuildResult r = build_template(
        "h", dir, dir, dir, "15.1-RELEASE", "amd64",
        "zroot/parishbsd/templates/base-15.1-RELEASE-amd64",
        ctsv, dir, dir, t.ops);
    assert(!r.succeeded);
    rm_rf(dir);
}

static void test_build_refuses_pid_lock() {
    std::string dir = "/tmp/parish-tb-pid";
    rm_rf(dir); mkdir_p(dir);
    setup_template_conf(dir, "p",
        "name=p\nstate=building\nclusters=T-core\n");
    // Pre-create pid file so O_EXCL fails.
    std::string pid_path = dir + "/template-build-p.pid";
    write_file(pid_path, "99999\n");

    TestOps t;
    t.path_exists_ret = true;
    BuildResult r = build_template(
        "p", dir, dir, dir, "15.1-RELEASE", "amd64", "",
        dir + "/clusters.tsv", dir, dir, t.ops);
    std::remove(pid_path.c_str());
    rm_rf(dir);
    assert(!r.succeeded);
    bool found = false;
    for (const auto& s : r.steps) {
        if (s.name == "acquire build lock" && s.outcome == Outcome::Failed) {
            assert(s.detail.find("already running") != std::string::npos);
            found = true;
        }
    }
    assert(found && "pid-locked build must refuse at acquire build lock");
}

// ---------- main ----------

int main() {
    test_parser_rejects_space_separated();
    test_parser_rejects_unknown_requires();
    test_parser_accepts_valid();
    test_resolver_puts_appuser_first();
    test_resolver_detects_cycle();
    test_state_file_atomic_roundtrip();
    test_state_file_preserves_unknown_keys();
    test_build_refuses_invalid_name();
    test_build_refuses_staging_exists();
    test_build_happy_path_fake_ops();
    test_build_fails_when_clone_fails();
    test_build_fails_when_provision_fails();
    test_build_refuses_pid_lock();
    std::printf("all unit tests passed\n");
    return 0;
}