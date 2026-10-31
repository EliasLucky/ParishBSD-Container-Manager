#ifndef PARISH_CLUSTERS_H
#define PARISH_CLUSTERS_H

#include <map>
#include <string>
#include <vector>

namespace parish {
namespace tbuild {

enum class ClusterKind { Required, Optional, Preset };

struct Cluster {
    std::string name;                        // e.g. "T-xpra", "P-office"
    ClusterKind kind = ClusterKind::Optional;
    std::vector<std::string> packages;       // empty if packages="-"
    std::vector<std::string> requires;       // names of required clusters
    std::vector<std::string> provisions;     // hook names, e.g. "app-user"
    bool no_pkg_install = false;             // true iff packages was "-"
};

struct ClustersDb {
    std::map<std::string, Cluster> by_name;
};

// Parse clusters.tsv. Strict: fails on any malformed line, on tab-vs-space
// violation, on duplicate names, on unknown requires: target.
// Returns false and sets error on failure.
bool parse_clusters_tsv(const std::string& path, ClustersDb& out, std::string& error);

// Resolve a requested component list to the transitive closure,
// in dependency-first order. T-appuser is forced first (F49).
// Presets are kept in order at the end (they have no depends-on relationships).
// Returns false and sets error on: unknown cluster, missing required cluster,
// cyclic requires, T-appuser not in the resolved set when any other T-* is present.
bool resolve_clusters(const std::vector<std::string>& requested,
                      const ClustersDb& db,
                      std::vector<Cluster>& out,
                      std::string& error);

}  // namespace tbuild
}  // namespace parish

#endif