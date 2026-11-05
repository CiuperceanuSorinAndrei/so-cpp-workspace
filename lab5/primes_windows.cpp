#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

bool readAll(HANDLE handle, void* data, DWORD size) {
    char* p = static_cast<char*>(data);

    while (size > 0) {
        DWORD received = 0;

        if (!ReadFile(handle, p, size, &received, nullptr) ||
            received == 0) {
            return false;
        }

        p += received;
        size -= received;
    }

    return true;
}

bool writeAll(HANDLE handle, const void* data, DWORD size) {
    const char* p = static_cast<const char*>(data);

    while (size > 0) {
        DWORD sent = 0;

        if (!WriteFile(handle, p, size, &sent, nullptr) ||
            sent == 0) {
            return false;
        }

        p += sent;
        size -= sent;
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

int runChild() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

    int interval[2];

    if (!readAll(input, interval, sizeof(interval))) {
        return 1;
    }

    CloseHandle(input);

    for (int n = interval[0]; n <= interval[1]; ++n) {
        if (isPrime(n) && !writeAll(output, &n, sizeof(n))) {
            return 1;
        }
    }

    int end = 0;
    bool sent = writeAll(output, &end, sizeof(end));

    CloseHandle(output);
    return sent ? 0 : 1;
}

struct Child {
    HANDLE process;
    HANDLE result;
    DWORD pid;
};

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--worker") {
        return runChild();
    }

    char executable[MAX_PATH];
    DWORD length = GetModuleFileNameA(
        nullptr, executable, MAX_PATH
    );

    if (length == 0 || length >= MAX_PATH) {
        std::cerr << "Nu am putut obtine calea executabilului.\n";
        return 1;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    std::vector<Child> children;
    children.reserve(10);
    bool success = true;

    for (int i = 0; i < 10; ++i) {
        HANDLE taskRead, taskWrite;
        HANDLE resultRead, resultWrite;

        if (!CreatePipe(&taskRead, &taskWrite, &security, 0)) {
            std::cerr << "CreatePipe task: " << GetLastError() << '\n';
            success = false;
            break;
        }

        if (!CreatePipe(&resultRead, &resultWrite, &security, 0)) {
            std::cerr << "CreatePipe result: " << GetLastError() << '\n';
            CloseHandle(taskRead);
            CloseHandle(taskWrite);
            success = false;
            break;
        }

        if (!SetHandleInformation(taskWrite, HANDLE_FLAG_INHERIT, 0) ||
            !SetHandleInformation(resultRead, HANDLE_FLAG_INHERIT, 0)) {
            std::cerr << "SetHandleInformation: "
                      << GetLastError() << '\n';
            CloseHandle(taskRead);
            CloseHandle(taskWrite);
            CloseHandle(resultRead);
            CloseHandle(resultWrite);
            success = false;
            break;
        }

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = taskRead;
        startup.hStdOutput = resultWrite;
        startup.hStdError = resultWrite;

        PROCESS_INFORMATION process{};

        std::string command =
            "\"" + std::string(executable) + "\" --worker";

        BOOL created = CreateProcessA(
            executable,
            command.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process
        );

        if (!created) {
            std::cerr << "CreateProcess: " << GetLastError() << '\n';
            CloseHandle(taskRead);
            CloseHandle(taskWrite);
            CloseHandle(resultRead);
            CloseHandle(resultWrite);
            success = false;
            break;
        }

        CloseHandle(process.hThread);
        CloseHandle(taskRead);
        CloseHandle(resultWrite);

        children.push_back({
            process.hProcess, resultRead, process.dwProcessId
        });

        int interval[2] = {i * 1000 + 1, (i + 1) * 1000};

        if (!writeAll(taskWrite, interval, sizeof(interval))) {
            std::cerr << "Nu am putut trimite intervalul.\n";
            success = false;
        }

        CloseHandle(taskWrite);
    }

    int total = 0;

    for (const Child& child : children) {
        int prime;
        bool finished = false;

        while (readAll(child.result, &prime, sizeof(prime))) {
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

        CloseHandle(child.result);
    }

    for (const Child& child : children) {
        DWORD exitCode = 1;

        if (WaitForSingleObject(child.process, INFINITE) != WAIT_OBJECT_0 ||
            !GetExitCodeProcess(child.process, &exitCode) ||
            exitCode != 0) {
            std::cerr << "\nProcesul " << child.pid
                      << " nu a terminat cu succes.\n";
            success = false;
        }

        CloseHandle(child.process);
    }

    std::cout << "\nTotal numere prime: " << total << '\n';
    return success ? 0 : 1;
}