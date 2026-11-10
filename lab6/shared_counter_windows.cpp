#include <windows.h>
#include <cstdio>
#include <ctime>
#include <random>
#include <string>

int reportError(const char* operation) {
    std::fprintf(
        stderr, "%s: %lu\n",
        operation, GetLastError()
    );
    return 1;
}

int countNumbers(int* counter, HANDLE semaphore) {
    std::mt19937 generator(
        static_cast<unsigned int>(std::time(nullptr)) ^
        static_cast<unsigned int>(GetCurrentProcessId())
    );
    std::uniform_int_distribution<int> coin(1, 2);

    while (true) {
        if (WaitForSingleObject(semaphore, INFINITE)
            != WAIT_OBJECT_0) {
            return reportError("WaitForSingleObject semaphore");
        }

        int current = *counter;

        while (current < 1000 && coin(generator) == 2) {
            ++current;
            *counter = current;

            std::printf(
                "PID %lu: %d\n",
                GetCurrentProcessId(), current
            );
        }

        bool finished = current == 1000;

        if (!ReleaseSemaphore(semaphore, 1, nullptr)) {
            return reportError("ReleaseSemaphore");
        }

        if (finished) {
            return 0;
        }
    }
}

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    bool worker =
        argc == 3 && std::string(argv[1]) == "--worker";

    if (argc != 1 && !worker) {
        std::fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    std::string id = worker
        ? argv[2]
        : std::to_string(GetCurrentProcessId());

    std::string memoryName = "Local\\SO_Lab6_Memory_" + id;
    std::string semaphoreName = "Local\\SO_Lab6_Semaphore_" + id;

    HANDLE mapping = worker
        ? OpenFileMappingA(
            FILE_MAP_ALL_ACCESS, FALSE, memoryName.c_str()
        )
        : CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(int), memoryName.c_str()
        );

    if (!mapping) {
        return reportError("FileMapping");
    }

    auto* counter = static_cast<int*>(
        MapViewOfFile(
            mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int)
        )
    );

    if (!counter) {
        int result = reportError("MapViewOfFile");
        CloseHandle(mapping);
        return result;
    }

    HANDLE semaphore = worker
        ? OpenSemaphoreA(
            SYNCHRONIZE | SEMAPHORE_MODIFY_STATE,
            FALSE, semaphoreName.c_str()
        )
        : CreateSemaphoreA(
            nullptr, 1, 1, semaphoreName.c_str()
        );

    if (!semaphore) {
        int result = reportError("Semaphore");
        UnmapViewOfFile(counter);
        CloseHandle(mapping);
        return result;
    }

    int result = 0;

    if (worker) {
        result = countNumbers(counter, semaphore);
    } else {
        *counter = 0;

        char executable[MAX_PATH];
        DWORD length = GetModuleFileNameA(
            nullptr, executable, MAX_PATH
        );

        if (length == 0 || length >= MAX_PATH) {
            std::fprintf(stderr, "Cannot obtain executable path.\n");
            result = 1;
        } else {
            std::string command =
                "\"" + std::string(executable) +
                "\" --worker " + id;

            STARTUPINFOA startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

            PROCESS_INFORMATION process{};

            if (!CreateProcessA(
                executable, command.data(),
                nullptr, nullptr, TRUE, 0,
                nullptr, nullptr, &startup, &process
            )) {
                result = reportError("CreateProcess");
            } else {
                CloseHandle(process.hThread);

                std::printf(
                    "Procese participante: %lu si %lu\n",
                    GetCurrentProcessId(), process.dwProcessId
                );

                result = countNumbers(counter, semaphore);

                DWORD waited = WaitForSingleObject(
                    process.hProcess, INFINITE
                );
                DWORD exitCode = 1;

                if (waited != WAIT_OBJECT_0) {
                    result = reportError("WaitForSingleObject child");
                } else if (!GetExitCodeProcess(
                    process.hProcess, &exitCode
                )) {
                    result = reportError("GetExitCodeProcess");
                } else if (exitCode != 0) {
                    std::fprintf(
                        stderr,
                        "Procesul copil nu a terminat cu succes.\n"
                    );
                    result = 1;
                }

                CloseHandle(process.hProcess);

                std::printf("Valoare finala: %d\n", *counter);

                if (*counter != 1000) {
                    result = 1;
                }
            }
        }
    }

    if (!UnmapViewOfFile(counter)) {
        result = reportError("UnmapViewOfFile");
    }

    if (!CloseHandle(semaphore)) {
        result = reportError("CloseHandle semaphore");
    }

    if (!CloseHandle(mapping)) {
        result = reportError("CloseHandle mapping");
    }

    return result;
}