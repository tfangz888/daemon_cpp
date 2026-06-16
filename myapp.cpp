// g++ -std=c++17 myapp.cpp -o myapp -pthread
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <sys/eventfd.h>
#include <unistd.h>
#include <sys/epoll.h>

using namespace std;

// ===================== worker =====================
void worker_main(atomic<bool>& running) {
    cout << "[worker] started\n";

    while (running) {
        cout << "[worker] running...\n";
        sleep(10);
    }

    cout << "[worker] stopped\n";
}

// ===================== worker wrapper =====================
class Worker {
public:
    atomic<bool> running{false};
    thread th;

    void start() {
        if (running) return;

        running = true;
        th = thread([this]() {
            worker_main(running);
        });

        cout << "[daemon] worker started\n";
    }

    void stop() {
        if (!running) return;

        running = false;
        if (th.joinable()) th.join();

        cout << "[daemon] worker stopped\n";
    }
};

Worker worker;

// ===================== eventfd =====================
int efd;

// ===================== signal flags =====================
enum Action {
    ACT_START = 1,
    ACT_STOP  = 2,
    ACT_EXIT  = 3
};

// 写 eventfd
void notify(Action a) {
    uint64_t v = a;
    write(efd, &v, sizeof(v));
}

// signal handler（只做写 eventfd）
void handle(int sig) {
    if (sig == SIGUSR1) notify(ACT_START);
    if (sig == SIGUSR2) notify(ACT_STOP);
    if (sig == SIGTERM) notify(ACT_EXIT);
}

// ===================== daemon loop =====================
void loop() {

    int ep = epoll_create1(0);

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = efd;

    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev);

    while (true) {

        epoll_event events[1];

        // 阻塞等待事件（无 polling）
        int n = epoll_wait(ep, events, 1, -1);

        if (n > 0) {

            uint64_t msg;
            read(efd, &msg, sizeof(msg));

            if (msg == ACT_START) {
                cout << "[signal] START\n";
                worker.start();
            }

            else if (msg == ACT_STOP) {
                cout << "[signal] STOP\n";
                worker.stop();
            }

            else if (msg == ACT_EXIT) {
                cout << "[signal] EXIT\n";
                worker.stop();
                break;
            }
        }
    }

    close(ep);
}

// ===================== main =====================
int main() {

    efd = eventfd(0, EFD_NONBLOCK);

    signal(SIGUSR1, handle);
    signal(SIGUSR2, handle);
    signal(SIGTERM, handle);

    cout << "[daemon] started\n";

    loop();

    cout << "[daemon] exited\n";
    close(efd);

    return 0;
}

