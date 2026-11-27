#pragma once

#include <functional>
#include <chrono>
#include <deque>
#include <map>
#include <set>
#include <shared_mutex>
#include <string>
#include <vector>
#include <triolib.hpp>

struct WorkList;

/// @brief A single work item. It is a function that will be called on a thread pool.
struct Work {
    using WorkFunc = std::function<bool(WorkList& list)>;

    virtual ~Work() = default;
    Work(uint priority, WorkFunc work) : priority(priority), work(work) {};
    /**
     * @brief How often it gets chosen on a certain
     * thread.
     * @detail Think of this variable as the amount of
     * lottery tickets bought. A higher number means
     * this one is chosen more than the others, but
     * never all the time.
     */
    uint priority;
    /**
     * @brief Work function.
     * Returns false if it did not find any
     * work.
     * @note This work and the parent work list is shared locked in this
     * scenario
     */
    WorkFunc work;
};

struct MutexWork : Work {
    MutexWork(uint priority, WorkFunc work) : Work(priority, work) {};
    std::shared_mutex workmutex;
};

/// @brief A list of work items that can be executed on a thread pool, with priority.
struct WorkList {
    std::shared_mutex listmutex;
    std::map<std::string, std::unique_ptr<MutexWork>> works;
    /// @brief Randomly chooses a work.
    std::string get_work(const std::set<std::string>& blacklist = {}, bool lock = true);

    /// @brief one-off tasks that will be completed and removed. Names start with `!`
    std::map<std::string, std::unique_ptr<Work>> tasks;
    std::shared_mutex tasks_mutex;

    /**
     * @brief Adds the work specified.
     * @param name The name of the work to add. Cannot be empty. Cannot be duplicated.
     * Cannot start with a `!`
     * @note Auto unique locks \ref mutex
     * @throws std::logic_error If \ref name is blank or already exists
     */
    void add_work(std::function<bool(WorkList& list)> work, std::string_view name, uint priority);
    /**
     * @brief Adds a one-off task to the list. It will be removed after it is completed.
     * @param work The task to add. Cannot be empty.
     * @param name The name of the task to add. Cannot be empty. Cannot be duplicated.
     * Must start with a `!`
     * @param priority The priority of the work. Higher means more likely to be chosen.
     * @note Auto unique locks \ref tasks_mutex
     */
    void add_task(std::function<bool(WorkList& list)> work, std::string_view name, uint priority);

    /**
     * @brief Checks if the work exists
     * @note Auto shared locks \ref mutex
     */
    bool has_work(std::string_view name);

    struct WorkStat {
        std::chrono::steady_clock::time_point last_update;
        std::chrono::nanoseconds working_time{};
        mutable std::deque<std::pair<std::chrono::steady_clock::time_point, std::chrono::nanoseconds>> completed_work;
    };
    mutable std::mutex stats_mutex;
    std::map<std::string, WorkStat> stats;
    std::map<std::string, double> working_time_per_second() const;
    /// @return Average duration in seconds of completed work actions from the last second.
    std::map<std::string, double> average_work_time() const;
    void record_work(std::string_view name, std::chrono::nanoseconds working_time, bool completed);
    /**
     * @brief Removes the work specified.
     * @param name The name of the work to remove. If blank or already removed, it will ignore.
     * @note Auto unique locks \ref mutex
     */
    void remove_work(std::string_view name);
};

/// @brief Uses RAII to add and remove work from a specified work list.
struct WorkAdder {
    WorkList& main_list;
    std::string name;
    WorkAdder(WorkList& main_list, std::function<bool(WorkList& list)> work, std::string_view name, uint priority)
        : main_list(main_list), name(name) {
        main_list.add_work(work, name, priority);
    }

    ~WorkAdder() { main_list.remove_work(name); }
};

/// @brief The main thread pool, with a main work list.
struct ThreadPool : triolib::SubApp {
    tClass;

    uint thread_count = 0;
    WorkList main_list;

    virtual std::vector<double> thread_work_per_second() = 0;
    virtual void shutdown() = 0;
};
