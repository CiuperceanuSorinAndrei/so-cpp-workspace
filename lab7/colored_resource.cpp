#include <iostream>
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <cstddef>
#include <exception>

std::mutex outputMutex;

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

std::vector<Event> events;
std::deque<Request> waitingRequests;
std::condition_variable accessChanged;
int activeCount;
Color activeColor = Color::Alb;

bool canEnter(int id, Color color)
{
    if (waitingRequests.size() == 0)
        return false;
    if (waitingRequests.front().id != id)
        return false;
    if (activeCount == 0)
        return true;
    if (activeColor == color)
        return true;
    else
        return false;
}

void admitFront(Color color)
{
    activeCount++;
    activeColor = color;
    waitingRequests.pop_front();
}

void enterResource(int id, Color color)
{
    std::unique_lock<std::mutex> stateLock(outputMutex);
    waitingRequests.push_back(Request{id, color});
    events.push_back(Event{EventKind::Request, id, color, activeCount});
    while (canEnter(id, color) == false)
        accessChanged.wait(stateLock);
    admitFront(color);
    events.push_back(Event{EventKind::Enter, id, color, activeCount});
    accessChanged.notify_all();
}

void leaveResource(int id, Color color)
{
    std::lock_guard<std::mutex> outputLock(outputMutex);
    activeCount--;
    events.push_back(Event{EventKind::Exit, id, color, activeCount});
    if (activeCount == 0)
        accessChanged.notify_all();
}

void runThread(int id, Color color)
{
    enterResource(id, color);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    leaveResource(id, color);
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
    std::vector<std::thread> workers;
    bool launchFailed = false;

    try
    {
        workers.reserve(workload.size());
        int id = 1;
        for (Color color : workload)
        {
            workers.emplace_back(runThread, id, color);
            ++id;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Pornirea firelor a esuat: " << error.what() << '\n';
        launchFailed = true;
    }

    for (std::thread& worker : workers)
        worker.join();

    printEvents();
    if (launchFailed)
        return 1;

    std::cout << "Toate firele au terminat\n";
}
