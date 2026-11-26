#include <pthread.h>
#include <time.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>

enum class Color
{
    Alb,
    Negru
};

struct Request
{
    int id;
    Color color;
};

enum class EventKind
{
    Request,
    Enter,
    Exit
};

struct Event
{
    EventKind kind;
    int id;
    Color color;
    int activeCount;
};

pthread_mutex_t stateMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t accessChanged = PTHREAD_COND_INITIALIZER;
std::vector<Event> events;
std::deque<Request> waitingRequests;
int activeCount = 0;
Color activeColor = Color::Alb;

void checkPthread(int error, const char* operation)
{
    if (error != 0)
    {
        std::fprintf(stderr, "%s: %s\n", operation, std::strerror(error));
        std::abort();
    }
}

bool canEnter(int id, Color color)
{
    if (waitingRequests.empty())
        return false;
    if (waitingRequests.front().id != id)
        return false;
    if (activeCount == 0)
        return true;
    return activeColor == color;
}

void admitFront(Color color)
{
    activeCount++;
    activeColor = color;
    waitingRequests.pop_front();
}

void enterResource(int id, Color color)
{
    checkPthread(pthread_mutex_lock(&stateMutex), "pthread_mutex_lock");
    waitingRequests.push_back(Request{id, color});
    events.push_back(Event{EventKind::Request, id, color, activeCount});
    while (!canEnter(id, color))
        checkPthread(pthread_cond_wait(&accessChanged, &stateMutex), "pthread_cond_wait");
    admitFront(color);
    events.push_back(Event{EventKind::Enter, id, color, activeCount});
    checkPthread(pthread_cond_broadcast(&accessChanged), "pthread_cond_broadcast");
    checkPthread(pthread_mutex_unlock(&stateMutex), "pthread_mutex_unlock");
}

void leaveResource(int id, Color color)
{
    checkPthread(pthread_mutex_lock(&stateMutex), "pthread_mutex_lock");
    --activeCount;
    events.push_back(Event{EventKind::Exit, id, color, activeCount});
    if (activeCount == 0)
        checkPthread(pthread_cond_broadcast(&accessChanged), "pthread_cond_broadcast");
    checkPthread(pthread_mutex_unlock(&stateMutex), "pthread_mutex_unlock");
}

void* runThread(void* argument)
{
    const Request& request = *static_cast<const Request*>(argument);
    enterResource(request.id, request.color);

    timespec delay{0, 100000000L};
    timespec remaining{};
    while (nanosleep(&delay, &remaining) == -1)
    {
        if (errno != EINTR)
        {
            std::perror("nanosleep");
            std::abort();
        }
        delay = remaining;
    }

    leaveResource(request.id, request.color);
    return nullptr;
}

void printEvents()
{
    std::size_t sequence = 1;

    for (const Event& event : events)
    {
        std::cout << sequence++ << ' ';
        switch (event.kind)
        {
            case EventKind::Request:
                std::cout << "REQUEST";
                break;
            case EventKind::Enter:
                std::cout << "ENTER";
                break;
            case EventKind::Exit:
                std::cout << "EXIT";
                break;
        }

        const char* colorName = event.color == Color::Alb ? "Alb" : "Negru";
        std::cout << ' ' << colorName << event.id
                  << " active=" << event.activeCount << '\n';
    }
}

int main()
{
    const std::vector<Color> workload{Color::Alb, Color::Alb, Color::Negru, Color::Alb};
    std::vector<Request> arguments;
    arguments.reserve(workload.size());
    events.reserve(3 * workload.size());
    int id = 1;
    for (Color color : workload)
    {
        arguments.push_back(Request{id, color});
        ++id;
    }

    std::vector<pthread_t> workers(arguments.size());
    std::size_t started = 0;
    int launchError = 0;
    for (Request& request : arguments)
    {
        launchError = pthread_create(&workers[started], nullptr, runThread, &request);
        if (launchError != 0)
            break;
        ++started;
    }

    for (std::size_t index = 0; index < started; ++index)
        checkPthread(pthread_join(workers[index], nullptr), "pthread_join");

    checkPthread(pthread_cond_destroy(&accessChanged), "pthread_cond_destroy");
    checkPthread(pthread_mutex_destroy(&stateMutex), "pthread_mutex_destroy");
    printEvents();
    if (launchError != 0)
    {
        std::fprintf(stderr, "pthread_create: %s\n", std::strerror(launchError));
        return 1;
    }

    std::cout << "Toate firele au terminat\n";
}
