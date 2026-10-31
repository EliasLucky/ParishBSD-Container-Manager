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
#include <csignal>
#include <map>
#include <fcntl.h>
#include <cerrno>
#include <sys/select.h>

using namespace parish::tbuild;

struct WizardState {
    std::string name;
    std::string description;
    std::string extra_packages;
    int compat_idx = 0; // 0=None, 1=Linux, 2=Windows, 3=Both
    std::vector<Cluster> components;
    std::vector<Cluster> presets;
    std::vector<bool> comp_selected;
    std::vector<bool> preset_selected;
};

volatile sig_atomic_t g_cancel_requested = 0;

void sigint_handler(int) { g_cancel_requested = 1; }

std::map<std::string, std::string> get_labels() {
    return {
        {"T-core", "Base system"}, {"T-xpra", "Graphical environment"},
        {"T-fonts-min", "Base fonts"}, {"T-appuser", "Application user (security)"},
        {"T-compat-linux", "Linux compatibility"}, {"T-compat-windows", "Windows compatibility (Wine)"},
        {"T-file-tools", "File manager and text editor"}, {"T-postgres", "PostgreSQL Database"},
        {"T-audio-sndio", "Audio support"}, {"P-browser-esr", "Web browser (Firefox)"},
        {"P-browser-lite", "Web browser (Lite)"}, {"P-office", "Office suite (LibreOffice)"},
        {"P-dev-tools", "Development tools"}
    };
}

void init_ui() {
    initscr(); start_color(); init_pair(1, COLOR_RED, COLOR_BLACK);
    cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0);
}

void cleanup_ui() { endwin(); }

WINDOW* create_win(int height, int width, const char* title) {
    int max_y, max_x; getmaxyx(stdscr, max_y, max_x);
    int start_y = (max_y - height) / 2; int start_x = (max_x - width) / 2;
    clear(); refresh();
    WINDOW* win = newwin(height, width, start_y, start_x);
    keypad(win, TRUE); box(win, 0, 0);
    wattron(win, A_BOLD); mvwprintw(win, 0, 2, "[ %s ]", title); wattroff(win, A_BOLD);
    return win;
}

void draw_buttons(WINDOW* win, bool back_enabled, const char* cont_label, int active_btn) {
    int y, x; getmaxyx(win, y, x); int btn_y = y - 2;
    if (active_btn == 1) wattron(win, A_REVERSE);
    mvwprintw(win, btn_y, x - 15, "[ %s ]", cont_label);
    if (active_btn == 1) wattroff(win, A_REVERSE);
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

void show_help() {
    WINDOW* win = create_win(16, 70, "Help");
    mvwprintw(win, 2, 2, "System Components make your compartment work.");
    mvwprintw(win, 3, 2, "Every compartment needs 'Base system' and 'Graphical environment'.");
    mvwprintw(win, 4, 2, "Without fonts, programs that use text will crash.");
    mvwprintw(win, 6, 2, "Applications are the programs you will use.");
    mvwprintw(win, 7, 2, "The ones you check here are installed before first use.");
    mvwprintw(win, 8, 2, "You can also add more programs later with 'parishctl apps'.");
    mvwprintw(win, 12, 2, "Press any key to close.");
    wrefresh(win);
    wgetch(win);
    delwin(win);
}

// Validate extra package syntax and version pins
bool validate_extra_packages(const std::string& pkgs, std::string& error) {
    if (pkgs.empty()) return true;
    std::string cur;
    for (char c : pkgs) {
        if (c == ' ') {
            if (!cur.empty()) {
                if (cur.find('@') != std::string::npos || cur.find('=') != std::string::npos) {
                    error = "Version pins (@, =) are not allowed: " + cur; return false;
                }
                // Require a dot after the digit group (e.g. -12.0)
                for (size_t i = 0; i + 2 < cur.length(); ++i) {
                    if (cur[i] == '-' && isdigit(cur[i+1]) && cur[i+2] == '.') {
                        error = "Version pins (-X.Y) are not allowed: " + cur; return false;
                    }
                }
            }
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        if (cur.find('@') != std::string::npos || cur.find('=') != std::string::npos) {
            error = "Version pins (@, =) are not allowed: " + cur; return false;
        }
        for (size_t i = 0; i + 2 < cur.length(); ++i) {
            if (cur[i] == '-' && isdigit(cur[i+1]) && cur[i+2] == '.') {
                error = "Version pins (-X.Y) are not allowed: " + cur; return false;
            }
        }
    }
    return true;
}

bool screen_name_desc(WizardState& st) {
    int active_field = 0; bool name_error = false;
    while (true) {
        WINDOW* win = create_win(12, 70, "Create Template: Step 1 of 4 (Identity)");
        mvwprintw(win, 2, 2, "Name (a-z, 0-9, hyphen):");
        if (active_field == 0) wattron(win, A_REVERSE);
        mvwprintw(win, 3, 4, "[ %-40s ]", st.name.c_str());
        if (active_field == 0) wattroff(win, A_REVERSE);
        
        if (name_error) {
            wattron(win, A_BOLD | COLOR_PAIR(1));
            mvwprintw(win, 4, 4, "Invalid. Use lowercase letters, numbers, hyphens.");
            wattroff(win, A_BOLD | COLOR_PAIR(1));
        }
        
        mvwprintw(win, 5, 2, "Description (optional):");
        if (active_field == 1) wattron(win, A_REVERSE);
        mvwprintw(win, 6, 4, "[ %-40s ]", st.description.c_str());
        if (active_field == 1) wattroff(win, A_REVERSE);
        
        draw_buttons(win, false, "Continue", active_field - 2); wrefresh(win);
        int ch = wgetch(win);
        if (ch == KEY_DOWN || ch == '\t') { active_field = (active_field + 1) % 4; name_error = false; }
        else if (ch == KEY_UP || ch == KEY_BTAB) { active_field = (active_field + 3) % 4; name_error = false; }
        else if (ch == '\n' || ch == KEY_ENTER) {
            if (active_field == 2) { delwin(win); return false; }
            if (active_field == 3) {
                bool valid = !st.name.empty() && st.name.length() <= 63;
                for (char c : st.name) { if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) valid = false; }
                if (!st.name.empty() && (st.name.front() == '-' || st.name.back() == '-' || st.name.find("--") != std::string::npos)) valid = false;
                if (!valid) { name_error = true; continue; }
                delwin(win); return true;
            }
        } else if (active_field < 2 && (ch == KEY_BACKSPACE || ch == 127)) {
            if (active_field == 0 && !st.name.empty()) st.name.pop_back();
            if (active_field == 1 && !st.description.empty()) st.description.pop_back();
        } else if (active_field < 2 && isprint(ch)) {
            if (active_field == 0 && st.name.length() < 63) st.name += ch;
            if (active_field == 1 && st.description.length() < 40) st.description += ch;
        }
        delwin(win);
    }
}

bool screen_components(WizardState& st) {
    auto labels = get_labels(); int highlight = 0, scroll = 0; int n = st.components.size(); int active_btn = 2;
    static const int compat_cols[] = {17, 27, 38, 51}; 
    while (true) {
        WINDOW* win = create_win(18, 70, "Create Template: Step 2 of 4 (System Components)");
        mvwprintw(win, 2, 2, "Select components (F1 for Help):");
        mvwprintw(win, 2, 55, "[ F1 Help ]");
        
        int visible_items = 9;
        for (int i = 0; i < visible_items && (scroll + i) < n; ++i) {
            int idx = scroll + i;
            if (active_btn == 2 && idx == highlight) wattron(win, A_REVERSE);
            const auto& c = st.components[idx];
            const char* mark = st.comp_selected[idx] ? "[X]" : "[ ]";
            std::string label = labels.count(c.name) ? labels[c.name] : c.name;
            mvwprintw(win, 4 + i, 4, "%s %-25s", mark, label.c_str());
            if (active_btn == 2 && idx == highlight) wattroff(win, A_REVERSE);
        }
        
        if (scroll > 0) mvwprintw(win, 4, 40, "^^^");
        if (scroll + visible_items < n) mvwprintw(win, 4 + visible_items - 1, 40, "vvv");
        
        mvwprintw(win, 14, 2, "Compatibility: ( ) None  ( ) Linux  ( ) Windows  ( ) Both");
        if (active_btn == 3) wattron(win, A_REVERSE);
        mvwprintw(win, 14, compat_cols[st.compat_idx], "(*)");
        if (active_btn == 3) wattroff(win, A_REVERSE);
        
        draw_buttons(win, true, "Continue", active_btn == 3 ? 1 : active_btn); wrefresh(win);
        
        int ch = wgetch(win);
        if (ch == KEY_F(1)) { show_help(); continue; }
        else if (ch == '\t') { active_btn = (active_btn + 1) % 4; if (active_btn == 2) active_btn = 3; }
        else if (ch == KEY_BTAB) { active_btn = (active_btn + 3) % 4; if (active_btn == 2) active_btn = 1; }
        else if (active_btn == 3 && (ch == KEY_LEFT || ch == KEY_RIGHT)) {
            if (ch == KEY_LEFT && st.compat_idx > 0) st.compat_idx--;
            if (ch == KEY_RIGHT && st.compat_idx < 3) st.compat_idx++;
        } else if (ch == KEY_UP) {
            if (active_btn == 2 && highlight > 0) { highlight--; if (highlight < scroll) scroll--; }
        } else if (ch == KEY_DOWN) {
            if (active_btn == 2 && highlight < n - 1) { highlight++; if (highlight >= scroll + visible_items) scroll++; }
        } else if (ch == ' ' && active_btn == 2) {
            if (st.components[highlight].kind != ClusterKind::Required) {
                st.comp_selected[highlight] = !st.comp_selected[highlight];
                if (st.comp_selected[highlight]) {
                    for (const auto& req : st.components[highlight].requires_) {
                        for (int j = 0; j < n; ++j) if (st.components[j].name == req) st.comp_selected[j] = true;
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
    auto labels = get_labels(); int highlight = 0, scroll = 0; int n = st.presets.size(); if (n == 0) return true; int active_btn = 2;
    std::string error_msg;
    while (true) {
        WINDOW* win = create_win(18, 70, "Create Template: Step 3 of 4 (Applications)");
        mvwprintw(win, 2, 2, "Select applications:");
        int visible_items = 8;
        for (int i = 0; i < visible_items && (scroll + i) < n; ++i) {
            int idx = scroll + i;
            if (active_btn == 2 && idx == highlight) wattron(win, A_REVERSE);
            const auto& p = st.presets[idx];
            const char* mark = st.preset_selected[idx] ? "[X]" : "[ ]";
            std::string label = labels.count(p.name) ? labels[p.name] : p.name;
            mvwprintw(win, 4 + i, 4, "%s %s", mark, label.c_str());
            if (active_btn == 2 && idx == highlight) wattroff(win, A_REVERSE);
        }
        
        mvwprintw(win, 13, 2, "Extra packages (space-separated):");
        if (active_btn == 3) wattron(win, A_REVERSE);
        mvwprintw(win, 14, 4, "[ %-40s ]", st.extra_packages.c_str());
        if (active_btn == 3) wattroff(win, A_REVERSE);
        
        if (!error_msg.empty()) {
            wattron(win, A_BOLD | COLOR_PAIR(1));
            mvwprintw(win, 15, 4, "%s", error_msg.c_str());
            wattroff(win, A_BOLD | COLOR_PAIR(1));
        }
        
        draw_buttons(win, true, "Continue", active_btn == 3 ? 1 : active_btn); wrefresh(win);
        
        int ch = wgetch(win);
        if (ch == '\t') { active_btn = (active_btn + 1) % 4; if (active_btn == 2) active_btn = 3; }
        else if (ch == KEY_BTAB) { active_btn = (active_btn + 3) % 4; if (active_btn == 2) active_btn = 1; }
        else if (active_btn == 3) {
            if (ch == KEY_BACKSPACE || ch == 127) { if (!st.extra_packages.empty()) st.extra_packages.pop_back(); }
            else if (isprint(ch) && st.extra_packages.length() < 40) st.extra_packages += ch;
            error_msg.clear();
        } else if (ch == KEY_UP) {
            if (active_btn == 2 && highlight > 0) { highlight--; if (highlight < scroll) scroll--; }
        } else if (ch == KEY_DOWN) {
            if (active_btn == 2 && highlight < n - 1) { highlight++; if (highlight >= scroll + visible_items) scroll++; }
        } else if (ch == ' ' && active_btn == 2) {
            st.preset_selected[highlight] = !st.preset_selected[highlight];
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (active_btn == 0) { delwin(win); return false; }
            if (active_btn == 1) {
                if (!validate_extra_packages(st.extra_packages, error_msg)) { continue; }
                delwin(win); return true; 
            }
        }
        delwin(win);
    }
}

bool screen_review(WizardState& st) {
    auto labels = get_labels(); int active_btn = 1;
    while (true) {
        WINDOW* win = create_win(20, 70, "Create Template: Step 4 of 4 (Review)");
        mvwprintw(win, 2, 2, "Name: %s", st.name.c_str());
        mvwprintw(win, 3, 2, "Desc: %s", st.description.empty() ? "(none)" : st.description.c_str());
        
        static const char* compat_names[] = {"None", "Linux", "Windows", "Both"};
        mvwprintw(win, 4, 2, "Compat: %s", compat_names[st.compat_idx]);
        
        int y = 6;
        mvwprintw(win, y++, 2, "Components:");
        for (size_t i = 0; i < st.components.size(); ++i) {
            if (st.comp_selected[i]) {
                if (y < 12) {
                    std::string label = labels.count(st.components[i].name) ? labels[st.components[i].name] : st.components[i].name;
                    mvwprintw(win, y++, 4, "- %s", label.c_str());
                }
            }
        }
        
        y = 13;
        mvwprintw(win, y++, 2, "Applications:");
        for (size_t i = 0; i < st.presets.size(); ++i) {
            if (st.preset_selected[i]) {
                if (y < 16) {
                    std::string label = labels.count(st.presets[i].name) ? labels[st.presets[i].name] : st.presets[i].name;
                    mvwprintw(win, y++, 4, "- %s", label.c_str());
                }
            }
        }
        
        if (!st.extra_packages.empty()) mvwprintw(win, 17, 2, "Extra: %s", st.extra_packages.c_str());
        
        draw_buttons(win, true, "Build", active_btn); wrefresh(win);
        
        int ch = wgetch(win);
        if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == '\t' || ch == KEY_BTAB) active_btn = (active_btn + 1) % 2;
        else if (ch == '\n' || ch == KEY_ENTER) {
            if (active_btn == 0) { delwin(win); return false; }
            if (active_btn == 1) { delwin(win); return true; }
        }
        delwin(win);
    }
}

void run_build(const WizardState& st) {
    std::string dir = "/var/db/parishbsd/templates/" + st.name;
    std::string conf_path = dir + "/template.conf";
    
    struct stat sb;
    if (stat(conf_path.c_str(), &sb) == 0) {
        endwin();
        std::cerr << "parish: template '" << st.name << "' already exists.\n";
        std::cerr << "        To rebuild, run: parishctl template reset " << st.name << "\n";
        return;
    }
    
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        endwin(); std::cerr << "parish: cannot create state dir: " << strerror(errno) << "\n"; return;
    }
    int fd = open(conf_path.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (fd < 0) { endwin(); std::cerr << "parish: cannot open template.conf: " << strerror(errno) << "\n"; return; }
    
    std::string conf = "name=" + st.name + "\n";
    conf += "description.en=" + st.description + "\n";
    conf += "clusters=";
    for (size_t i = 0; i < st.components.size(); ++i) if (st.comp_selected[i]) conf += st.components[i].name + ",";
    
    if (st.compat_idx == 1) conf += "T-compat-linux,";
    if (st.compat_idx == 2) conf += "T-compat-windows,";
    if (st.compat_idx == 3) conf += "T-compat-linux,T-compat-windows,";
    
    conf += "\napplications=";
    for (size_t i = 0; i < st.presets.size(); ++i) if (st.preset_selected[i]) conf += st.presets[i].name + ",";
    
    conf += "\nextra_packages=" + st.extra_packages + "\ncompat=none\n";
    (void)!write(fd, conf.c_str(), conf.length()); close(fd);
    
    cleanup_ui();
    std::cout << "\nStarting build process...\n\n";
    
    struct sigaction sa{}; sa.sa_handler = sigint_handler; sigaction(SIGINT, &sa, NULL);
    
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/usr/local/sbin/parish-template-build", "parish-template-build", st.name.c_str(), "--source=user", (char*)NULL);
        _exit(1);
    }
    
    int status = 0;
    while (true) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) break;
        
        if (g_cancel_requested) {
            g_cancel_requested = 0;
            flushinp();
            std::cout << "\n\nCancel the build? [y/N] (default N): ";
            fd_set rfds; struct timeval tv;
            FD_ZERO(&rfds); FD_SET(0, &rfds); tv.tv_sec = 2; tv.tv_usec = 0;
            int ret = select(1, &rfds, NULL, NULL, &tv);
            if (ret > 0) {
                char c = getchar();
                if (c == 'y' || c == 'Y') {
                    std::cout << "Cancelling build...\n";
                    kill(-pid, SIGTERM);
                    for (int i = 0; i < 50; ++i) {
                        if (waitpid(pid, &status, WNOHANG) == pid) break;
                        struct timespec ts = {0, 100000000}; nanosleep(&ts, nullptr);
                    }
                    if (waitpid(pid, &status, WNOHANG) == 0) kill(-pid, SIGKILL);
                    break;
                }
            }
            std::cout << "Continuing build...\n";
        }
        struct timespec ts = {0, 50000000}; nanosleep(&ts, nullptr);
    }
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) std::cout << "\nBuild succeeded! Press Enter.\n";
    else std::cerr << "\nBuild failed/cancelled. Press Enter.\n";
    std::cin.get();
}

int main() {
    ClustersDb db; std::string err;
    if (!parse_clusters_tsv("/usr/local/libexec/parishbsd/templates/clusters.tsv", db, err)) {
        std::cerr << "Failed to load clusters.tsv: " << err << "\n"; return 1;
    }

    WizardState st;
    for (const auto& kv : db.by_name) {
        if (kv.second.kind == ClusterKind::Preset) {
            st.presets.push_back(kv.second); st.preset_selected.push_back(false);
        } else {
            st.components.push_back(kv.second); st.comp_selected.push_back(kv.second.kind == ClusterKind::Required);
        }
    }

    std::sort(st.components.begin(), st.components.end(), [](const Cluster& a, const Cluster& b){ return a.name < b.name; });
    std::sort(st.presets.begin(), st.presets.end(), [](const Cluster& a, const Cluster& b){ return a.name < b.name; });

    init_ui();
    int step = 1;
    while (step >= 1 && step <= 4) {
        if (step == 1) { if (screen_name_desc(st)) step = 2; else break; }
        else if (step == 2) { if (screen_components(st)) step = 3; else step = 1; }
        else if (step == 3) { if (screen_presets(st)) step = 4; else step = 2; }
        else if (step == 4) { if (screen_review(st)) { run_build(st); return 0; } else step = 3; }
    }
    cleanup_ui();
    return 0;
}