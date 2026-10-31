// Unit tests for parser and resolver (no root, no ZFS, no pkg).
// Run by default in CI.
//
// Integration tests for the full build flow (needs root, ZFS, pkg).
// Gated on PARISH_INTEGRATION=1. Uses a fake Ops that records calls
// and simulates success/failure; does NOT touch real ZFS.

#include "../clusters.h"
#include "../template_build.h"
#include "../state_file.h"

#include <cassert>
#include <cstdio>
#include <fstream>

using namespace parish::tbuild;

static void test_parser_rejects_space_separated() {
    std::string path = "/tmp/clusters.tsv.test";
    std::ofstream(path) << "T-core required - -\n";
    ClustersDb db; std::string err;
    bool ok = parse_clusters_tsv(path, db, err);
    std::remove(path.c_str());
    assert(!ok && "space-separated must be rejected");
}

static void test_parser_rejects_unknown_requires() {
    std::string path = "/tmp/clusters.tsv.test";
    std::ofstream(path)
        << "T-core\trequired\t-\t-\n"
        << "T-xpra\trequired\txpra\trequires:T-fonts-min\n";
    ClustersDb db; std::string err;
    bool ok = parse_clusters_tsv(path, db, err);
    std::remove(path.c_str());
    assert(!ok && "unknown requires: target must be rejected");
}

static void test_resolver_puts_appuser_first() {
    std::string path = "/tmp/clusters.tsv.test";
    std::ofstream(path)
        << "T-appuser\trequired\t-\tprovision:app-user\n"
        << "T-fonts-min\trequired\tfontconfig\t-\n"
        << "T-xpra\trequired\txpra\trequires:T-fonts-min\n";
    ClustersDb db; std::string err;
    assert(parse_clusters_tsv(path, db, err));
    std::vector<Cluster> out;
    assert(resolve_clusters({"T-xpra"}, db, out, err));
    std::remove(path.c_str());
    assert(!out.empty() && out[0].name == "T-appuser");
    // T-fonts-min must precede T-xpra.
    size_t fonts_idx = 0, xpra_idx = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i].name == "T-fonts-min") fonts_idx = i;
        if (out[i].name == "T-xpra")      xpra_idx = i;
    }
    assert(fonts_idx < xpra_idx);
}

static void test_state_file_atomic() {
    std::string path = "/tmp/state.test";
    std::map<std::string, std::string> kv = {{"name","x"},{"state","building"}};
    std::string err;
    assert(write_state_file_atomic(path, kv, err));
    std::map<std::string, std::string> parsed;
    assert(parse_state_file(path, parsed, err));
    std::remove(path.c_str());
    assert(parsed["state"] == "building");
}

static void test_build_fails_when_staging_exists() {
    // Fake Ops: zfs_dataset_exists(staging) returns true.
    Ops ops;
    ops.zfs_dataset_exists = [](const std::string&) { return true; };
    // (setup a template.conf)
    std::string dir = "/tmp/parish-test-tpl";
    system(("mkdir -p " + dir + "/x").c_str());
    std::ofstream(dir + "/x/template.conf")
        << "name=x\nstate=building\nclusters=T-core\n";
    BuildResult r = build_template("x", dir, "/dev/null", dir, dir, ops);
    system(("rm -rf " + dir).c_str());
    assert(!r.succeeded);
    assert(r.steps.back().name == "check name availability");
}

int main() {
    test_parser_rejects_space_separated();
    test_parser_rejects_unknown_requires();
    test_resolver_puts_appuser_first();
    test_state_file_atomic();
    test_build_fails_when_staging_exists();
    std::printf("all unit tests passed\n");
    return 0;
}