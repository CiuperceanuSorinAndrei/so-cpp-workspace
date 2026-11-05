#include <cerrno>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

bool readAll(int fd, void* data, size_t size) {
    char* p = static_cast<char*>(data);
    while (size > 0) {
        ssize_t n = read(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool writeAll(int fd, const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size > 0) {
        ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool isPrime(int n) {
    if (n < 2) return false;
    for (int d = 2; d <= n / d; ++d) {
        if (n % d == 0) return false;
    }
    return true;
}

struct Child {
    pid_t pid;
    int resultFd;
};

int main() {
    signal(SIGPIPE, SIG_IGN);
    std::vector<Child> children;
    children.reserve(10);
    bool success = true;

    for (int i = 0; i < 10; ++i) {
        int task[2], result[2];

        if (pipe(task) == -1) {
            perror("pipe task");
            success = false;
            break;
        }

        if (pipe(result) == -1) {
            perror("pipe result");
            close(task[0]);
            close(task[1]);
            success = false;
            break;
        }

        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            close(task[0]);
            close(task[1]);
            close(result[0]);
            close(result[1]);
            success = false;
            break;
        }

        if (pid == 0) {
            close(task[1]);
            close(result[0]);

            for (const Child& child : children) {
                close(child.resultFd);
            }

            int interval[2];

            if (!readAll(task[0], interval, sizeof(interval))) {
                _exit(1);
            }

            close(task[0]);

            for (int n = interval[0]; n <= interval[1]; ++n) {
                if (isPrime(n) && !writeAll(result[1], &n, sizeof(n))) {
                    _exit(1);
                }
            }

            int end = 0;
            bool sent = writeAll(result[1], &end, sizeof(end));
            close(result[1]);
            _exit(sent ? 0 : 1);
        }

        close(task[0]);
        close(result[1]);
        children.push_back({pid, result[0]});

        int interval[2] = {i * 1000 + 1, (i + 1) * 1000};

        if (!writeAll(task[1], interval, sizeof(interval))) {
            std::cerr << "Nu am putut trimite intervalul.\n";
            success = false;
        }

        close(task[1]);
    }

    int total = 0;

    for (const Child& child : children) {
        int prime;
        bool finished = false;

        while (readAll(child.resultFd, &prime, sizeof(prime))) {
            if (prime == 0) {
                finished = true;
                break;
            }

            std::cout << prime << ' ';
            ++total;
        }

        if (!finished) {
            std::cerr << "\nRezultat incomplet de la PID "
                      << child.pid << '\n';
            success = false;
        }

        close(child.resultFd);
    }

    for (const Child& child : children) {
        int status;
        pid_t waited;

        do {
            waited = waitpid(child.pid, &status, 0);
        } while (waited == -1 && errno == EINTR);

        if (waited == -1 || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            std::cerr << "\nProcesul " << child.pid
                      << " nu a terminat cu succes.\n";
            success = false;
        }
    }

    std::cout << "\nTotal numere prime: " << total << '\n';
    return success ? 0 : 1;
}
