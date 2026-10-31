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
    std::string description;
    std::vector<Cluster> components;
    std::vector<Cluster> presets;
    std::vector<bool> comp_selected;
    std::vector<bool> preset_selected;
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

// Helper to create a centered window with a title
WINDOW* create_win(int height, int width, const char* title) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int start_y = (max_y - height) / 2;
    int start_x = (max_x - width) / 2;

    WINDOW* win = newwin(height, width, start_y, start_x);
    box(win, 0, 0);
    
    // Draw title
    wattron(win, A_BOLD);
    mvwprintw(win, 0, 2, "[ %s ]", title);
    wattroff(win, A_BOLD);
    
    return win;
}

// Draws standard Back/Continue buttons at the bottom of the window
void draw_buttons(WINDOW* win, bool back_enabled, const char* cont_label, int active_btn) {
    int y, x;
    getmaxyx(win, y, x);
    int btn_y = y - 2;

    // Continue / Build button
    if (active_btn == 1) wattron(win, A_REVERSE);
    mvwprintw(win, btn_y, x - 15, "[ %s ]", cont_label);
    if (active_btn == 1) wattroff(win, A_REVERSE);

    // Back / Cancel button
    if (back_enabled) {
        if (active_btn == 0) wattron(win, A_REVERSE);
        mvwprintw(win, btn_y, x - 30, "[ Back ]");
        if (active_btn == 0) wattroff(win, A_REVERSE);
    } else {
        if (active_btn == 0) wattron(win, A_REVERSE);
        mvwprintw(win, btn_y, x - 30, "[ Cancel ]");
        if (active_btn == 0) wattroff(win, A_REVERSE);
    }
}

bool screen_name_desc(WizardState& st) {
    int active_field = 0; // 0=Name, 1=Desc, 2=Cancel, 3=Continue
    while (true) {
        WINDOW* win = create_win(12, 70, "Create Template: Step 1 of 4 (Identity)");
        
        mvwprintw(win, 2, 2, "Name (a-z, 0-9, hyphen):");
        if (active_field == 0) wattron(win, A_REVERSE);
        mvwprintw(win, 3, 4, "[ %-40s ]", st.name.c_str());
        if (active_field == 0) wattroff(win, A_REVERSE);

        mvwprintw(win, 5, 2, "Description (optional):");
        if (active_field == 1) wattron(win, A_REVERSE);
        mvwprintw(win, 6, 4, "[ %-40s ]", st.description.c_str());
        if (active_field == 1) wattroff(win, A_REVERSE);

        draw_buttons(win, false, "Continue", active_field - 2);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == KEY_DOWN || ch == '\t') active_field = (active_field + 1) % 4;
        else if (ch == KEY_UP || ch == KEY_BTAB) active_field = (active_field + 3) % 4;
        else if (ch == '\n' || ch == KEY_ENTER) {
            if (active_field == 2) { delwin(win); return false; } // Cancel
            if (active_field == 3) {
                if (st.name.empty()) continue; // Basic validation
                delwin(win); return true;
            }
        } else if (active_field < 2 && (ch == KEY_BACKSPACE || ch == 127)) {
            if (active_field == 0 && !st.name.empty()) st.name.pop_back();
            if (active_field == 1 && !st.description.empty()) st.description.pop_back();
        } else if (active_field < 2 && isprint(ch)) {
            if (active_field == 0 && st.name.length() < 31) st.name += ch;
            if (active_field == 1 && st.description.length() < 40) st.description += ch;
        }
        delwin(win);
    }
}

bool screen_components(WizardState& st) {
    int highlight = 0;
    int n = st.components.size();
    int active_btn = 1; // 0=Back, 1=Continue

    while (true) {
        WINDOW* win = create_win(18, 70, "Create Template: Step 2 of 4 (System Components)");
        
        mvwprintw(win, 2, 2, "Select components (Space to toggle):");
        for (int i = 0; i < n && i < 10; ++i) {
            if (active_btn == 2 && i == highlight) wattron(win, A_REVERSE);
            const auto& c = st.components[i];
            const char* mark = st.comp_selected[i] ? "[X]" : "[ ]";
            mvwprintw(win, 4 + i, 4, "%s %s", mark, c.name.c_str());
            if (active_btn == 2 && i == highlight) wattroff(win, A_REVERSE);
        }

        draw_buttons(win, true, "Continue", active_btn);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == KEY_UP) {
            if (active_btn == 2 && highlight > 0) highlight--;
            else if (active_btn == 2 && highlight == 0) active_btn = 1;
            else if (active_btn == 1) active_btn = 0;
        } else if (ch == KEY_DOWN || ch == '\t') {
            if (active_btn == 0) active_btn = 1;
            else if (active_btn == 1) active_btn = 2;
            else if (active_btn == 2 && highlight < n - 1) highlight++;
        } else if (ch == ' ' && active_btn == 2) {
            if (st.components[highlight].kind != ClusterKind::Required) {
                st.comp_selected[highlight] = !st.comp_selected[highlight];
                if (st.comp_selected[highlight]) {
                    for (const auto& req : st.components[highlight].requires_) {
                        for (int j = 0; j < n; ++j) {
                            if (st.components[j].name == req) st.comp_selected[j] = true;
                        }
                    }
                }
            }
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (active_btn == 0) { delwin(win); return false; }
            if (active_btn == 1) { delwin(win); return true; }
        }
        delwin(win);
    }
}

bool screen_presets(WizardState& st) {
    int highlight = 0;
    int n = st.presets.size();
    if (n == 0) return true; // Skip if no presets
    int active_btn = 1; // 0=Back, 1=Continue

    while (true) {
        WINDOW* win = create_win(18, 70, "Create Template: Step 3 of 4 (Applications)");
        
        mvwprintw(win, 2, 2, "Select presets (Space to toggle):");
        for (int i = 0; i < n && i < 10; ++i) {
            if (active_btn == 2 && i == highlight) wattron(win, A_REVERSE);
            const auto& p = st.presets[i];
            const char* mark = st.preset_selected[i] ? "[X]" : "[ ]";
            mvwprintw(win, 4 + i, 4, "%s %s", mark, p.name.c_str());
            if (active_btn == 2 && i == highlight) wattroff(win, A_REVERSE);
        }

        draw_buttons(win, true, "Continue", active_btn);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == KEY_UP) {
            if (active_btn == 2 && highlight > 0) highlight--;
            else if (active_btn == 2 && highlight == 0) active_btn = 1;
            else if (active_btn == 1) active_btn = 0;
        } else if (ch == KEY_DOWN || ch == '\t') {
            if (active_btn == 0) active_btn = 1;
            else if (active_btn == 1) active_btn = 2;
            else if (active_btn == 2 && highlight < n - 1) highlight++;
        } else if (ch == ' ' && active_btn == 2) {
            st.preset_selected[highlight] = !st.preset_selected[highlight];
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (active_btn == 0) { delwin(win); return false; }
            if (active_btn == 1) { delwin(win); return true; }
        }
        delwin(win);
    }
}

bool screen_review(WizardState& st) {
    int active_btn = 1; // 0=Back, 1=Build

    while (true) {
        WINDOW* win = create_win(20, 70, "Create Template: Step 4 of 4 (Review)");
        
        mvwprintw(win, 2, 2, "Name: %s", st.name.c_str());
        mvwprintw(win, 3, 2, "Desc: %s", st.description.empty() ? "(none)" : st.description.c_str());
        
        mvwprintw(win, 5, 2, "Components:");
        int y = 6;
        for (size_t i = 0; i < st.components.size(); ++i) {
            if (st.comp_selected[i]) mvwprintw(win, y++, 4, "- %s", st.components[i].name.c_str());
        }
        
        mvwprintw(win, y + 1, 2, "Applications:");
        y += 2;
        bool has_presets = false;
        for (size_t i = 0; i < st.presets.size(); ++i) {
            if (st.preset_selected[i]) {
                mvwprintw(win, y++, 4, "- %s", st.presets[i].name.c_str());
                has_presets = true;
            }
        }
        if (!has_presets) mvwprintw(win, y++, 4, "(none)");

        draw_buttons(win, true, "Build", active_btn);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == '\t') active_btn = (active_btn + 1) % 2;
        else if (ch == '\n' || ch == KEY_ENTER) {
            if (active_btn == 0) { delwin(win); return false; }
            if (active_btn == 1) { delwin(win); return true; }
        }
        delwin(win);
    }
}

void run_build(const WizardState& st) {
    std::string dir = "/var/db/parishbsd/templates/" + st.name;
    std::string cmd_mkdir = "mkdir -p " + dir;
    system(cmd_mkdir.c_str());

    std::string conf_path = dir + "/template.conf";
    std::ofstream conf(conf_path);
    conf << "name=" << st.name << "\n";
    conf << "state=building\n";
    conf << "description.en=" << st.description << "\n";
    
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
    conf << "compat=none\n";
    conf.close();

    // Hand over to the C++ builder
    cleanup_ui();
    std::cout << "\nStarting build process...\n\n";
    
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

    std::sort(st.components.begin(), st.components.end(), [](const Cluster& a, const Cluster& b){ return a.name < b.name; });
    std::sort(st.presets.begin(), st.presets.end(), [](const Cluster& a, const Cluster& b){ return a.name < b.name; });

    init_ui();

    // Flow control (allows going back)
    int step = 1;
    while (step >= 1 && step <= 4) {
        if (step == 1) {
            if (screen_name_desc(st)) step = 2; else break;
        } else if (step == 2) {
            if (screen_components(st)) step = 3; else step = 1;
        } else if (step == 3) {
            if (screen_presets(st)) step = 4; else step = 2;
        } else if (step == 4) {
            if (screen_review(st)) { run_build(st); return 0; } else step = 3;
        }
    }

    cleanup_ui();
    return 0;
}