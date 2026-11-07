#include "task.hpp"
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <set>
#include <ui/debug_screen.hpp>
#include "triolib.hpp"

using namespace std;
using namespace triolib;

string WorkList::get_work(const set<string>& blacklist, bool lock) {
    // 1 in 5 chance for every work / task to have the same priority
    // stops greedy work (high priority) from being chosen all the time
    bool all_equal = (rand() % 5) == 0;

    if (lock) {
        shared_lock __(listmutex);
        shared_lock __(tasks_mutex);
        return get_work(blacklist, false);
    }

    vector<string_view> tickets;
    for (auto& [name, work] : works) {
        if (blacklist.contains(name)) continue;
        shared_lock __(work->workmutex);
        if (all_equal)
            tickets.emplace_back(name);
        else
            for (uint i = 0; i < work->priority; i++) tickets.emplace_back(name);
    }
    for (auto& [name, task] : tasks) {
        if (blacklist.contains(name)) continue;
        if (all_equal)
            tickets.emplace_back(name);
        else
            for (uint i = 0; i < task->priority; i++) tickets.emplace_back(name);
    }

    if (tickets.empty()) return {};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, tickets.size() - 1);
    size_t random_index = dist(gen);
    return string{tickets[random_index]};
}

bool WorkList::has_work(std::string_view name) {
    shared_lock __(listmutex);

    return works.contains(std::string{name});
}

void WorkList::record_work(std::string_view name, chrono::nanoseconds working_time) {
    const auto now = chrono::steady_clock::now();
    lock_guard lock(stats_mutex);
    auto& stat = stats[std::string{name}];
    if (stat.last_update.time_since_epoch().count() == 0 || now - stat.last_update >= chrono::seconds(1)) {
        stat.working_time = chrono::nanoseconds::zero();
        stat.last_update = now;
    }
    stat.working_time += working_time;
}

map<string, double> WorkList::working_time_per_second() const {
    const auto now = chrono::steady_clock::now();
    lock_guard lock(stats_mutex);
    map<string, double> result;
    for (const auto& [name, stat] : stats) {
        const double elapsed = max(1e-6, chrono::duration<double>(now - stat.last_update).count());
        result[name] = chrono::duration<double>(stat.working_time).count() / elapsed;
    }
    return result;
}

void WorkList::add_work(function<bool(WorkList& list)> work, string_view name, uint priority) {
    unique_lock __(listmutex);

    if (name.empty()) throw std::logic_error("Work name \"" + std::string{name} + "\" cannot be empty");
    if (name.starts_with('!')) throw std::logic_error("Work name \"" + std::string{name} + "\"must not start with '!'");
    if (works.contains(std::string{name}))
        throw std::logic_error("Work name \"" + std::string{name} + "\" already exists");
    works.try_emplace(std::string{name}, std::make_unique<MutexWork>(priority, work));
}

void WorkList::add_task(function<bool(WorkList& list)> work, std::string_view name, uint priority) {
    unique_lock __(tasks_mutex);

    if (name.empty()) throw std::logic_error("Task name \"" + std::string{name} + "\" cannot be empty");
    if (!name.starts_with('!')) throw std::logic_error("Task name \"" + std::string{name} + "\" must start with '!'");
    if (tasks.contains(std::string{name}))
        throw std::logic_error("Task name \"" + std::string{name} + "\" already exists");
    tasks.try_emplace(std::string{name}, std::make_unique<Work>(priority, work));
}

void WorkList::remove_work(std::string_view name) {
    unique_lock __(listmutex);

    if (name.empty()) return;
    if (!works.contains(std::string{name})) return;
    works.erase(std::string{name});
}

struct ThreadPoolImpl : ThreadPool {
    tImplClass(ThreadPool);

    bool run_work() {
        shared_lock __(main_list.listmutex);
        string candidate;
        {
            shared_lock tasks_lock(main_list.tasks_mutex);
            candidate = main_list.get_work({}, false);
        }
        if (candidate.empty()) return false;

        Work* work = nullptr;
        unique_ptr<Work> task;
        if (candidate[0] == '!') {
            unique_lock tasks_lock(main_list.tasks_mutex);
            auto it = main_list.tasks.find(candidate);
            if (it == main_list.tasks.end()) return false;
            task = std::move(it->second);
            main_list.tasks.erase(it);
            work = task.get();
        } else {
            auto it = main_list.works.find(candidate);
            if (it == main_list.works.end()) return false;
            shared_lock work_lock(it->second->workmutex);
            const auto start = chrono::steady_clock::now();
            const bool worked = it->second->work(main_list);
            const auto elapsed = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - start);
            main_list.record_work(candidate, elapsed);
            return worked;
        }

        const auto start = chrono::steady_clock::now();
        const bool worked = work->work(main_list);
        const auto elapsed = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - start);
        main_list.record_work(candidate, elapsed);
        return worked;
    }

    set<size_t> thread_ids;
    mutex thread_ids_mutex;
    vector<unique_ptr<atomic<uint64_t>>> busy_nanoseconds;
    chrono::steady_clock::time_point usage_started;

    vector<double> thread_work_per_second() override {
        const auto now = chrono::steady_clock::now();
        const double elapsed = max(1e-6, chrono::duration<double>(now - usage_started).count());
        usage_started = now;
        vector<double> result;
        for (const auto& value : busy_nanoseconds)
            result.push_back(static_cast<double>(value->exchange(0)) / 1e9 / elapsed);
        return result;
    }

    void run_thread(std::stop_token& stop) {
        size_t this_id = [&] {
            lock_guard __(thread_ids_mutex);
            for (size_t i = 0;; i++) {
                if (!thread_ids.contains(i)) {
                    thread_ids.insert(i);
                    return i;
                }
            }
        }();
        while (!stop.stop_requested()) {
            const auto started = chrono::steady_clock::now();
            bool worked = false;
            try {
                worked = run_work();
            } catch (const std::exception& e) {
                console.err << "Thread " << this_id << " threw a " << get_dynamic_name(e) << "\n" << e.what();
            }
            busy_nanoseconds[this_id]->fetch_add(
                static_cast<uint64_t>(
                    chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - started).count()),
                memory_order_relaxed);
            if (!worked) this_thread::sleep_for(chrono::milliseconds(5));
        }
        lock_guard __(thread_ids_mutex);
        thread_ids.erase(this_id);
    }

    std::vector<std::jthread> thread_pool;
    bool stopped = false;

    void resize_thread_pool() {
        if (thread_pool.size() > thread_count) {
            for (uint i = thread_count; i < thread_pool.size(); ++i) thread_pool[i].request_stop();

            thread_pool.resize(thread_count);
            return;
        }

        thread_pool.reserve(thread_count);
        while (busy_nanoseconds.size() < thread_count)
            busy_nanoseconds.push_back(std::make_unique<atomic<uint64_t>>(0));

        while (thread_pool.size() < thread_count) {
            thread_pool.emplace_back([this](std::stop_token stop) { run_thread(stop); });
        }
    }

    void init() override {
        SubApp::init();
        stopped = false;
        const size_t hw = max<size_t>(1, thread::hardware_concurrency());
        if (thread_count == 0) thread_count = hw > 2 ? static_cast<uint>(hw - 2) : 1;
        usage_started = chrono::steady_clock::now();
        resize_thread_pool();
    }

    void loop() override {
        resize_thread_pool();
        if (auto* debug = application->get_subapp_ptr<DebugScreen>()) {
            debug->corner = DebugScreen::Corner::BottomRight;
            const auto usage = thread_work_per_second();
            for (size_t i = 0; i < usage.size(); ++i)
                debug->show_at(
                    DebugScreen::Corner::BottomRight, "Thread " + to_string(i), to_string(round(usage[i] * 100.0) / 100.0) + "s/s");
            for (const auto& [name, working_time] : main_list.working_time_per_second())
                debug->show_at(DebugScreen::Corner::TopRight, name, to_string(round(working_time * 100.0) / 100.0) + "s/s");
        }
    }

    void shutdown() override {
        if (stopped) return;
        stopped = true;
        for (auto& thread : thread_pool) thread.request_stop();
        thread_pool.clear();
    }

    void quit() override { shutdown(); }
};

REGISTER_SUBAPP(ThreadPoolImpl);
