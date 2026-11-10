#include <cerrno>
#include <cstdio>
#include <ctime>
#include <random>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

struct SharedData {
    sem_t semaphore;
    int counter;
};

int countNumbers(SharedData* shared) {
    std::mt19937 generator(
        static_cast<unsigned int>(std::time(nullptr)) ^
        static_cast<unsigned int>(getpid())
    );
    std::uniform_int_distribution<int> coin(1, 2);

    while (true) {
        while (sem_wait(&shared->semaphore) == -1) {
            if (errno != EINTR) {
                std::perror("sem_wait");
                return 1;
            }
        }

        int current = shared->counter;

        while (current < 1000 && coin(generator) == 2) {
            ++current;
            shared->counter = current;

            std::printf(
                "PID %ld: %d\n",
                static_cast<long>(getpid()),
                current
            );
        }

        bool finished = current == 1000;

        if (sem_post(&shared->semaphore) == -1) {
            std::perror("sem_post");
            return 1;
        }

        if (finished) {
            return 0;
        }
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    void* memory = mmap(
        nullptr,
        sizeof(SharedData),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );

    if (memory == MAP_FAILED) {
        std::perror("mmap");
        return 1;
    }

    auto* shared = static_cast<SharedData*>(memory);
    shared->counter = 0;

    if (sem_init(&shared->semaphore, 1, 1) == -1) {
        std::perror("sem_init");
        munmap(shared, sizeof(SharedData));
        return 1;
    }

    pid_t child = fork();

    if (child == -1) {
        std::perror("fork");
        sem_destroy(&shared->semaphore);
        munmap(shared, sizeof(SharedData));
        return 1;
    }

    if (child == 0) {
        int result = countNumbers(shared);
        munmap(shared, sizeof(SharedData));
        _exit(result);
    }

    std::printf(
        "Procese participante: %ld si %ld\n",
        static_cast<long>(getpid()),
        static_cast<long>(child)
    );

    int result = countNumbers(shared);
    int status = 0;
    pid_t waited;

    do {
        waited = waitpid(child, &status, 0);
    } while (waited == -1 && errno == EINTR);

    if (waited == -1) {
        std::perror("waitpid");
        return 1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::fprintf(stderr, "Procesul copil nu a terminat cu succes.\n");
        result = 1;
    }

    std::printf("Valoare finala: %d\n", shared->counter);

    if (shared->counter != 1000) {
        result = 1;
    }

    if (sem_destroy(&shared->semaphore) == -1) {
        std::perror("sem_destroy");
        result = 1;
    }

    if (munmap(shared, sizeof(SharedData)) == -1) {
        std::perror("munmap");
        result = 1;
    }

    return result;
}
