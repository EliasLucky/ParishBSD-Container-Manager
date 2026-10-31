#include "clusters.h"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace parish {
namespace tbuild {

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else { cur.push_back(c); }
    }
    out.push_back(cur);
    return out;
}

bool is_all_whitespace(const std::string& s) {
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return true;
}

ClusterKind parse_kind(const std::string& s) {
    if (s == "required") return ClusterKind::Required;
    if (s == "optional") return ClusterKind::Optional;
    if (s == "preset")   return ClusterKind::Preset;
    throw std::runtime_error("unknown kind: " + s);
}

}  // namespace

bool parse_clusters_tsv(const std::string& path,
                        ClustersDb& out,
                        std::string& error)
{
    std::ifstream in(path);
    if (!in) { error = "cannot open " + path; return false; }

    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (is_all_whitespace(line)) continue;

        if (line.find('\t') == std::string::npos) {
            error = path + ":" + std::to_string(lineno) +
                    ": no tab found (clusters.tsv must be tab-separated, not space)";
            return false;
        }

        auto f = split(line, '\t');
        if (f.size() != 4) {
            error = path + ":" + std::to_string(lineno) +
                    ": expected 4 tab-separated fields, got " +
                    std::to_string(f.size());
            return false;
        }

        Cluster c;
        c.name = f[0];
        if (c.name.empty()) {
            error = path + ":" + std::to_string(lineno) + ": empty cluster name";
            return false;
        }
        if (out.by_name.count(c.name)) {
            error = path + ":" + std::to_string(lineno) +
                    ": duplicate cluster name: " + c.name;
            return false;
        }

        try {
            c.kind = parse_kind(f[1]);
        } catch (const std::exception& e) {
            error = path + ":" + std::to_string(lineno) + ": " + e.what();
            return false;
        }

        if (f[2] == "-") {
            c.no_pkg_install = true;
        } else if (!f[2].empty()) {
            for (const auto& p : split(f[2], ',')) {
                if (!p.empty()) c.packages.push_back(p);
            }
        }

        if (f[3] != "-" && !f[3].empty()) {
            for (const auto& rule : split(f[3], ',')) {
                if (rule.empty()) continue;
                if (rule.rfind("requires:", 0) == 0) {
                    c.requires.push_back(rule.substr(9));
                } else if (rule.rfind("provision:", 0) == 0) {
                    c.provisions.push_back(rule.substr(10));
                } else if (rule.rfind("group:", 0) == 0) {
                    // accepted, unused
                } else {
                    error = path + ":" + std::to_string(lineno) +
                            ": unknown rule: " + rule;
                    return false;
                }
            }
        }

        out.by_name.emplace(c.name, std::move(c));
    }

    for (const auto& kv : out.by_name) {
        for (const auto& r : kv.second.requires) {
            if (!out.by_name.count(r)) {
                error = "cluster " + kv.first + " requires unknown cluster " + r;
                return false;
            }
        }
    }
    return true;
}

static bool visit(const std::string& name,
                  const ClustersDb& db,
                  std::set<std::string>& visited,
                  std::set<std::string>& visiting,
                  std::vector<Cluster>& out,
                  std::string& error)
{
    if (visited.count(name)) return true;
    if (visiting.count(name)) { error = "cycle detected at: " + name; return false; }
    auto it = db.by_name.find(name);
    if (it == db.by_name.end()) { error = "unknown cluster: " + name; return false; }
    visiting.insert(name);
    for (const auto& r : it->second.requires) {
        if (!visit(r, db, visited, visiting, out, error)) return false;
    }
    visiting.erase(name);
    visited.insert(name);
    out.push_back(it->second);
    return true;
}

bool resolve_clusters(const std::vector<std::string>& requested,
                      const ClustersDb& db,
                      std::vector<Cluster>& out,
                      std::string& error)
{
    out.clear();

    auto appuser_it = db.by_name.find("T-appuser");
    if (appuser_it == db.by_name.end()) {
        error = "clusters.tsv missing required cluster: T-appuser";
        return false;
    }

    std::vector<std::string> comps;
    std::vector<std::string> presets;
    for (const auto& r : requested) {
        if (r.rfind("P-", 0) == 0) presets.push_back(r);
        else                        comps.push_back(r);
    }
    comps.push_back("T-appuser");

    std::set<std::string> visited, visiting;
    std::vector<Cluster> resolved_comps;
    for (const auto& c : comps) {
        if (!visit(c, db, visited, visiting, resolved_comps, error)) return false;
    }

    out.push_back(*appuser_it);
    for (const auto& c : resolved_comps) {
        if (c.name == "T-appuser") continue;
        out.push_back(c);
    }
    for (const auto& p : presets) {
        auto it = db.by_name.find(p);
        if (it == db.by_name.end()) { error = "unknown preset: " + p; return false; }
        out.push_back(it->second);
    }
    return true;
}

}  // namespace tbuild
}  // namespace parish