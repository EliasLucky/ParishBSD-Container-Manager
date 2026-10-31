#include "clusters.h"

#include <ncurses.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace parish::tbuild;

struct WizardState {
    std::string name;
    std::vector<Cluster> components;
    std::vector<Cluster> presets;
    std::vector<bool> comp_selected;
    std::vector<bool> preset_selected;
    std::string compat = "none";
};

void init_ui() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
}

void cleanup_ui() {
    endwin();
}

void draw_header(const char* title) {
    clear();
    attron(A_BOLD | A_REVERSE);
    mvprintw(0, 0, " %-76s ", title);
    attroff(A_BOLD | A_REVERSE);
}

std::string get_input(int y, int x, int max_len) {
    curs_set(1);
    echo();
    char buf[256] = {0};
    mvgetnstr(y, x, buf, max_len);
    noecho();
    curs_set(0);
    return std::string(buf);
}

bool screen_name(WizardState& st) {
    draw_header("Create Template: Step 1 of 4 (Name)");
    mvprintw(2, 2, "Name: ");
    mvprintw(3, 2, "(a-z, 0-9, hyphen; 3-31 chars)");
    
    std::string name = get_input(2, 8, 31);
    if (name.empty()) return false;
    
    st.name = name;
    return true;
}

bool screen_components(WizardState& st, const ClustersDb& db) {
    int highlight = 0;
    int n_comps = st.components.size();

    while (true) {
        draw_header("Create Template: Step 2 of 4 (System Components)");
        mvprintw(2, 2, "Use UP/DOWN to navigate, SPACE to toggle, ENTER to continue.");
        
        for (int i = 0; i < n_comps; ++i) {
            if (i == highlight) attron(A_REVERSE);
            
            const auto& c = st.components[i];
            bool is_required = (c.kind == ClusterKind::Required);
            const char* mark = st.comp_selected[i] ? "[X]" : "[ ]";
            
            mvprintw(4 + i, 2, "%s %-20s", mark, c.name.c_str());
            
            if (i == highlight) attroff(A_REVERSE);
        }

        int ch = getch();
        if (ch == KEY_UP && highlight > 0) highlight--;
        else if (ch == KEY_DOWN && highlight < n_comps - 1) highlight++;
        else if (ch == ' ') {
            if (st.components[highlight].kind != ClusterKind::Required) {
                st.comp_selected[highlight] = !st.comp_selected[highlight];
                // Auto-resolve dependencies
                if (st.comp_selected[highlight]) {
                    for (const auto& req : st.components[highlight].requires_) {
                        for (int j = 0; j < n_comps; ++j) {
                            if (st.components[j].name == req) {
                                st.comp_selected[j] = true;
                            }
                        }
                    }
                }
            }
        }
        else if (ch == '\n' || ch == KEY_ENTER) {
            return true;
        }
    }
}

bool screen_presets(WizardState& st) {
    int highlight = 0;
    int n_presets = st.presets.size();
    if (n_presets == 0) return true;

    while (true) {
        draw_header("Create Template: Step 3 of 4 (Applications)");
        mvprintw(2, 2, "Use UP/DOWN to navigate, SPACE to toggle, ENTER to continue.");
        
        for (int i = 0; i < n_presets; ++i) {
            if (i == highlight) attron(A_REVERSE);
            const char* mark = st.preset_selected[i] ? "[X]" : "[ ]";
            mvprintw(4 + i, 2, "%s %-20s", mark, st.presets[i].name.c_str());
            if (i == highlight) attroff(A_REVERSE);
        }

        int ch = getch();
        if (ch == KEY_UP && highlight > 0) highlight--;
        else if (ch == KEY_DOWN && highlight < n_presets - 1) highlight++;
        else if (ch == ' ') {
            st.preset_selected[highlight] = !st.preset_selected[highlight];
        }
        else if (ch == '\n' || ch == KEY_ENTER) {
            return true;
        }
    }
}

bool screen_review(WizardState& st) {
    draw_header("Create Template: Step 4 of 4 (Review)");
    mvprintw(2, 2, "Name: %s", st.name.c_str());
    
    mvprintw(4, 2, "Components:");
    int y = 5;
    for (size_t i = 0; i < st.components.size(); ++i) {
        if (st.comp_selected[i]) {
            mvprintw(y++, 4, "- %s", st.components[i].name.c_str());
        }
    }
    
    mvprintw(y++, 2, "Applications:");
    for (size_t i = 0; i < st.presets.size(); ++i) {
        if (st.preset_selected[i]) {
            mvprintw(y++, 4, "- %s", st.presets[i].name.c_str());
        }
    }

    mvprintw(y + 2, 2, "Press ENTER to build, or 'q' to cancel.");
    int ch = getch();
    return (ch == '\n' || ch == KEY_ENTER);
}

void run_build(const WizardState& st) {
    // 1. Write template.conf
    std::string dir = "/var/db/parishbsd/templates/" + st.name;
    std::string cmd_mkdir = "mkdir -p " + dir;
    system(cmd_mkdir.c_str());

    std::string conf_path = dir + "/template.conf";
    std::ofstream conf(conf_path);
    conf << "name=" << st.name << "\n";
    conf << "state=building\n";
    
    conf << "clusters=";
    bool first = true;
    for (size_t i = 0; i < st.components.size(); ++i) {
        if (st.comp_selected[i]) {
            if (!first) conf << ",";
            conf << st.components[i].name;
            first = false;
        }
    }
    conf << "\n";

    conf << "applications=";
    first = true;
    for (size_t i = 0; i < st.presets.size(); ++i) {
        if (st.preset_selected[i]) {
            if (!first) conf << ",";
            conf << st.presets[i].name;
            first = false;
        }
    }
    conf << "\n";
    conf << "extra_packages=\n";
    conf << "compat=" << st.compat << "\n";
    conf.close();

    // 2. Clear screen and exec the builder
    clear();
    refresh();
    endwin(); // Hand terminal over to the builder

    std::string cmd = "/usr/local/sbin/parish-template-build " + st.name + " --source=user";
    int ret = system(cmd.c_str());
    
    if (ret != 0) {
        std::cerr << "\nBuild failed. Press Enter to exit.\n";
        std::cin.get();
    } else {
        std::cout << "\nBuild succeeded! Press Enter to exit.\n";
        std::cin.get();
    }
}

int main() {
    // Load clusters.tsv
    ClustersDb db;
    std::string err;
    if (!parse_clusters_tsv("/usr/local/libexec/parishbsd/templates/clusters.tsv", db, err)) {
        std::cerr << "Failed to load clusters.tsv: " << err << "\n";
        return 1;
    }

    WizardState st;
    for (const auto& kv : db.by_name) {
        if (kv.second.kind == ClusterKind::Preset) {
            st.presets.push_back(kv.second);
            st.preset_selected.push_back(false);
        } else {
            st.components.push_back(kv.second);
            st.comp_selected.push_back(kv.second.kind == ClusterKind::Required);
        }
    }

    // Sort alphabetically for display
    std::sort(st.components.begin(), st.components.end(), [](const Cluster& a, const Cluster& b){ return a.name < b.name; });
    std::sort(st.presets.begin(), st.presets.end(), [](const Cluster& a, const Cluster& b){ return a.name < b.name; });

    init_ui();

    if (screen_name(st) &&
        screen_components(st, db) &&
        screen_presets(st) &&
        screen_review(st)) 
    {
        run_build(st);
    } else {
        cleanup_ui();
    }

    return 0;
}