#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

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

CRITICAL_SECTION stateMutex;
CONDITION_VARIABLE accessChanged = CONDITION_VARIABLE_INIT;
std::vector<Event> events;
std::deque<Request> waitingRequests;
int activeCount = 0;
Color activeColor = Color::Alb;

[[noreturn]] void failWindows(const char* operation)
{
    const DWORD error = GetLastError();
    std::fprintf(stderr, "%s: Windows error %lu\n", operation, static_cast<unsigned long>(error));
    std::abort();
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
    EnterCriticalSection(&stateMutex);
    waitingRequests.push_back(Request{id, color});
    events.push_back(Event{EventKind::Request, id, color, activeCount});
    while (!canEnter(id, color))
    {
        if (!SleepConditionVariableCS(&accessChanged, &stateMutex, INFINITE))
            failWindows("SleepConditionVariableCS");
    }
    admitFront(color);
    events.push_back(Event{EventKind::Enter, id, color, activeCount});
    WakeAllConditionVariable(&accessChanged);
    LeaveCriticalSection(&stateMutex);
}

void leaveResource(int id, Color color)
{
    EnterCriticalSection(&stateMutex);
    --activeCount;
    events.push_back(Event{EventKind::Exit, id, color, activeCount});
    if (activeCount == 0)
        WakeAllConditionVariable(&accessChanged);
    LeaveCriticalSection(&stateMutex);
}

unsigned __stdcall runThread(void* argument)
{
    const Request& request = *static_cast<const Request*>(argument);
    enterResource(request.id, request.color);
    Sleep(100);
    leaveResource(request.id, request.color);
    return 0;
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

    std::vector<HANDLE> workers;
    workers.reserve(arguments.size());
    InitializeCriticalSection(&stateMutex);

    bool launchFailed = false;
    int launchError = 0;
    for (Request& request : arguments)
    {
        const auto handle = _beginthreadex(nullptr, 0, runThread, &request, 0, nullptr);
        if (handle == 0)
        {
            launchError = errno;
            launchFailed = true;
            break;
        }
        workers.push_back(reinterpret_cast<HANDLE>(handle));
    }

    for (HANDLE worker : workers)
    {
        if (WaitForSingleObject(worker, INFINITE) != WAIT_OBJECT_0)
            failWindows("WaitForSingleObject");
        if (!CloseHandle(worker))
            failWindows("CloseHandle");
    }

    DeleteCriticalSection(&stateMutex);
    printEvents();
    if (launchFailed)
    {
        std::fprintf(stderr, "_beginthreadex: %s\n", std::strerror(launchError));
        return 1;
    }

    std::cout << "Toate firele au terminat\n";
}
