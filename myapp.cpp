#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>
#include <ctime>
#include <unistd.h>

using namespace std;

// ===================== LOG (PID FILE) =====================
static ofstream g_log;

void log(const string& msg) {
    g_log << msg << endl;
    g_log.flush();
}

// ===================== TIME WINDOW =====================
struct Window {
    int start; // minutes
    int end;
};

// 当前时间（分钟）
int nowMin() {
    time_t t = time(nullptr);
    tm* lt = localtime(&t);
    return lt->tm_hour * 60 + lt->tm_min;
}

// HH:MM -> minutes
int toMin(const string& s) {
    int h = stoi(s.substr(0,2));
    int m = stoi(s.substr(3,2));
    return h * 60 + m;
}

// 解析配置
vector<Window> parseWindows(const string& line) {
    vector<Window> ws;

    auto pos = line.find('=');
    string s = (pos != string::npos) ? line.substr(pos + 1) : line;

    string item;
    stringstream ss(s);

    while (getline(ss, item, ',')) {
        auto dash = item.find('-');
        if (dash == string::npos) continue;

        string a = item.substr(0, dash);
        string b = item.substr(dash + 1);

        ws.push_back({toMin(a), toMin(b)});
    }

    return ws;
}

// 判断是否在窗口（支持跨天）
bool inWindow(const vector<Window>& ws) {
    int now = nowMin();

    for (auto& w : ws) {

        // ===== 正常区间 =====
        if (w.start <= w.end) {
            if (now >= w.start && now <= w.end)
                return true;
        }

        // ===== 跨天区间 =====
        else {
            // 22:00-02:00
            if (now >= w.start || now <= w.end)
                return true;
        }
    }

    return false;
}

// 读取配置
vector<Window> loadWindows(const string& path) {

    ifstream f(path);
    string line;

    while (getline(f, line)) {
        if (line.find("WINDOWS=") == 0)
            return parseWindows(line);
    }

    return {};
}

// ===================== WORKER =====================
void worker_main(atomic<bool>& running) {

    log("[worker] started");

    while (running) {

        log("[worker] running... tick every 10 seconds");

        for (int i = 0; i < 10; i++) {
            if (!running) break;
            this_thread::sleep_for(chrono::seconds(1));
        }
    }

    log("[worker] stopped");
}

// ===================== WORKER WRAPPER =====================
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

        log("[daemon] worker started");
    }

    void stop() {
        if (!running) return;

        running = false;

        if (th.joinable())
            th.join();

        log("[daemon] worker stopped");
    }

    bool isRunning() {
        return running;
    }
};

Worker worker;

// ===================== SIGNAL =====================
atomic<bool> g_exit{false};
atomic<bool> g_start{false};
atomic<bool> g_stop{false};

void handle(int sig) {
    if (sig == SIGTERM || sig == SIGINT)
        g_exit = true;

    if (sig == SIGUSR1)
        g_start = true;

    if (sig == SIGUSR2)
        g_stop = true;
}

// ===================== DAEMON LOOP =====================
void daemon_loop() {

    const string cfgPath = "/etc/myapp/windows.cfg";

    while (!g_exit) {

        // ===== signal start =====
        if (g_start) {
            log("[signal] force start worker");
            worker.start();
            g_start = false;
        }

        // ===== signal stop =====
        if (g_stop) {
            log("[signal] force stop worker");
            worker.stop();
            g_stop = false;
        }

        // ===== reload config =====
        vector<Window> windows = loadWindows(cfgPath);

        // ===== time window control =====
        if (inWindow(windows)) {
            if (!worker.isRunning())
                worker.start();
        } else {
            if (worker.isRunning())
                worker.stop();
        }

        this_thread::sleep_for(chrono::seconds(1));
    }

    log("[daemon] exiting...");
    worker.stop();
}

// ===================== MAIN =====================
int main() {

    // ===== PID LOG =====
    pid_t pid = getpid();
    string path = "/tmp/myapp_" + to_string(pid) + ".log";

    g_log.open(path, ios::app);

    if (!g_log.is_open()) {
        cerr << "cannot open log file\n";
        return 1;
    }

    log("[daemon] started");

    // ===== signals =====
    signal(SIGINT, handle);
    signal(SIGTERM, handle);
    signal(SIGUSR1, handle);
    signal(SIGUSR2, handle);

    // ===== run =====
    daemon_loop();

    log("[daemon] stopped");

    return 0;
}
