// cli/xpra-sup.cpp  Xpra session supervisor.
//
// Spawned by `parish-xpra start <jail>` after Xpra is running inside
// the jail. Responsibilities:
//   1. Create /var/run/parishbsd/xpra (tmpfs must exist per boot).
//   2. Write /var/run/parishbsd/xpra/<jail>.state with pid=<self> and
//      socket=<host path of the jail-side Xpra socket>.
//   3. Loop, checking every second whether the jail-side Xpra process
//      is still alive.
//   4. On SIGTERM/SIGINT, or when the jail-side Xpra dies:
//      - kill the jail-side Xpra if still alive (SIGTERM, 5s grace,
//        then SIGKILL)
//      - unlink the state file
//      - exit 0
//
// The state file is read by Guardian's containment::close_xpra to
// terminate the host-side wrapper when a jail is contained. Without
// it, close_xpra returns Skipped and the Xpra session survives
// containment.
//
// WHY COMPILED C++, NOT SHELL
// ---------------------------
// Guardian's close_xpra checks the host process's kinfo_proc.ki_comm
// (p_comm) for a name starting with "xpra" (strncmp(comm, "xpra", 4)
// == 0). A #!/bin/sh script's p_comm is "sh" (the interpreter's
// basename), which fails the check. A compiled binary named
// "xpra-sup" has p_comm = "xpra-sup", which satisfies strncmp.
//
// CONSTRAINTS
// -----------
// - Runs as root (parent is parish-xpra, invoked via pkexec from
//   parishctl-xpra-start).
// - Daemonizes: parent (parish-xpra) gets exit 0 immediately; child
//   supervises.
// - State file: /var/run/parishbsd/xpra/<jail>.state, mode 0600,
//   owner root. Guardian's close_xpra requires S_ISREG, uid=0,
//   mode 0600.
// - Atomic write: tmp + fsync + rename.
// - Refuses symlinks at the state file path (defense against a
//   jail-planted symlink).

#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr const char* kStateDir  = "/var/run/parishbsd/xpra";
constexpr const char* kJexecPath = "/usr/sbin/jexec";
constexpr int         kPollSec   = 1;
constexpr int         kKillGrace = 5;

volatile sig_atomic_t g_stop = 0;
void on_stop(int) { g_stop = 1; }

bool jexec_capture(const std::string& jail, const std::vector<std::string>& args, std::string& out)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);
        signal(SIGINT,  SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        std::vector<char*> c;
        c.push_back(const_cast<char*>(kJexecPath));
        c.push_back(const_cast<char*>(jail.c_str()));
        for (const auto& a : args)
            c.push_back(const_cast<char*>(a.c_str()));
        c.push_back(nullptr);
        execv(kJexecPath, c.data());
        _exit(127);
    }

    close(pipefd[1]);
    char buf[4096];
    ssize_t n;
    out.clear();
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(n));
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool jail_xpra_alive(const std::string& jail, const std::string& disp_arg)
{
    std::string out;
    if (!jexec_capture(jail, {"pgrep", "-f", "xpra.*" + disp_arg}, out))
        return false;
    return !out.empty();
}

bool write_state_file(const std::string& jail, int pid, const std::string& socket)
{
    std::string path = std::string(kStateDir) + "/" + jail + ".state";
    std::string tmp  = path + ".tmp." + std::to_string(getpid());

    struct stat st{};
    if (lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode))
        return false;

    int fd = open(tmp.c_str(),
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return false;

    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "pid=%d\nsocket=%s\n", pid, socket.c_str());
    if (n < 0 || write(fd, buf, static_cast<size_t>(n)) != n) {
        close(fd); unlink(tmp.c_str()); return false;
    }
    fsync(fd);
    close(fd);

    if (rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

void unlink_state_file(const std::string& jail)
{
    std::string path = std::string(kStateDir) + "/" + jail + ".state";
    unlink(path.c_str());
}

// Mirror of validate.sh::validate_jail_name_existing.
//
// C++/shell duplication is unavoidable in the current architecture
// there is no shared validator between the two languages. This is
// the same duplication class as Guardian's
// containment.cpp::valid_jail_name. Both implementations must be
// changed together if the rules change. The parity is tested by
// tests/fixtures/jail-names.txt.
bool valid_jail_name(const std::string& s)
{
    if (s.empty() || s.size() > 64) return false;
    if (s[0] == '.') return false;
    if (s.find("..") != std::string::npos) return false;
    for (unsigned char c : s) {
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.'))
            return false;
    }
    return true;
}

// Display numbers are digits only (Xpra local displays: 100, 101,
// ...). Rejecting non-digits closes two things:
//   1. pgrep pattern construction: "xpra.*:" + disp. A non-digit
//      makes the pattern match nothing (silent fail-open) or match
//      something unintended.
//   2. Socket path construction: "/var/run/xpra/" + jail + "/" +
//      disp + "/socket". A slash in disp would traverse out of
//      the per-jail directory.
bool valid_display(const std::string& s)
{
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: xpra-sup <jail> <display_num>\n");
        return 2;
    }
    std::string jail = argv[1];
    std::string disp = argv[2];
    std::string disp_arg = ":" + disp;

    // Input validation. Both arguments reach (a) filesystem paths,
    // (b) jexec argv, and (c) pgrep patterns. Validation happens
    // before the fork so the parent exits non-zero on bad input and
    // the caller (parish-xpra, or a terminal running it directly)
    // sees the error synchronously.
    //
    // Note: display_num must NOT include the leading colon. The
    // caller (parish-xpra) passes 100, not :100. The colon is added
    // here as part of disp_arg construction.
    if (!valid_jail_name(jail)) {
        fprintf(stderr, "xpra-sup: invalid jail name: %s\n", jail.c_str());
        return 2;
    }
    if (!valid_display(disp)) {
        fprintf(stderr,
                "xpra-sup: invalid display number: %s "
                "(digits only; do not include ':')", disp.c_str());
        return 2;
    }

    struct sigaction sa{};
    sa.sa_handler = on_stop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);

    // Daemonize. Parent (parish-xpra) exits with 0 immediately.
    pid_t dpid = fork();
    if (dpid < 0) {
        fprintf(stderr, "xpra-sup: fork: %s\n", strerror(errno));
        return 1;
    }
    if (dpid > 0) _exit(0);
    setsid();

    // Ensure /var/run/parishbsd/xpra exists. /var/run is tmpfs on
    // FreeBSD directories created by the port at install time do
    // not survive reboot (tmpfs(5): "data is lost when the file
    // system is unmounted"). xpra-sup must create the directory
    // itself at every startup.
    //
    // EEXIST is treated as success: the parent directory might have
    // been created by Guardian's respond_main or by parish-recover
    // at an earlier point in this boot. Compartments must not depend
    // on Guardian being installed; the mkdir is the correct
    // independent behavior either way.
    //
    // Mode 0700 root:wheel matches respond_main's mkdir of
    // /var/run/parishbsd and the pkg-plist @dir entry.
    //
    // if mkdir fails in the child, the
    // parent has already exited 0  the caller proceeds without a
    // state file. Unreachable in practice (root mkdir on tmpfs
    // cannot fail except on OOM). If a guarantee is ever
    // needed, use a fork+pipe handshake where the parent waits for
    // child OK/FAIL before exiting. Not done here per reviewer's
    // "parent exits as fast as possible" prescription.
    if (mkdir("/var/run/parishbsd", 0700) != 0 && errno != EEXIST) {
        fprintf(stderr,
                "xpra-sup: cannot create /var/run/parishbsd: %s\n",
                strerror(errno));
        return 1;
    }
    if (mkdir("/var/run/parishbsd/xpra", 0700) != 0 && errno != EEXIST) {
        fprintf(stderr,
                "xpra-sup: cannot create /var/run/parishbsd/xpra: %s\n",
                strerror(errno));
        return 1;
    }

    std::string socket_path =  "/var/run/xpra/" + jail + "/" + disp + "/socket";
    if (!write_state_file(jail, getpid(), socket_path)) {
        fprintf(stderr, "xpra-sup: cannot write state file\n");
        return 1;
    }

    // Supervisor loop. Sleeps in 1s increments; SIGTERM noticed
    // promptly because nanosleep returns EINTR.
    while (!g_stop) {
        struct timespec ts = { kPollSec, 0 };
        nanosleep(&ts, nullptr);
        if (g_stop) break;
        if (!jail_xpra_alive(jail, disp_arg)) break;
    }

    // Cleanup. Two paths reach here:
    //   (a) SIGTERM received (Guardian's close_xpra, user stop via
    //       parishctl-stop, admin kill). We are the only process
    //       that knows to shut down the jail-side Xpra; do so with
    //       SIGTERM, grace, SIGKILL.
    //   (b) jail-side Xpra died on its own. Nothing to kill.
    if (g_stop) {
        std::string out;
        jexec_capture(jail, {"pkill", "-TERM", "-f", "xpra.*" + disp_arg}, out);
        for (int i = 0; i < kKillGrace && jail_xpra_alive(jail, disp_arg); ++i) {
            struct timespec ts = { 1, 0 };
            nanosleep(&ts, nullptr);
        }
        if (jail_xpra_alive(jail, disp_arg))
            jexec_capture(jail, {"pkill", "-KILL", "-f", "xpra.*" + disp_arg}, out);
    }

    unlink_state_file(jail);
    return 0;
}