#include <dlfcn.h>
#include <cstdlib>
#include <signal.h>
#include <unistd.h>

// Test-only fault injection: simulate a child that remains alive after a kill
// request. This does not pretend that a userspace helper can ignore SIGKILL.
extern "C" int kill(pid_t pid, int signal)
{
    if (signal == SIGKILL && pid > 0 && std::getenv("DZURKEL_TEST_SUPPRESS_KILL")) {
        static constexpr char message[] = "SIMULATED_UNDELIVERED_SIGKILL\n";
        const auto written = ::write(STDERR_FILENO, message, sizeof(message) - 1);
        (void)written;
        return 0;
    }
    const auto realKill = reinterpret_cast<int (*)(pid_t, int)>(dlsym(RTLD_NEXT, "kill"));
    return realKill(pid, signal);
}
