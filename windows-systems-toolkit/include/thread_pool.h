// thread_pool.h
//
// A wrapper around the native Windows Thread Pool API (CreateThreadpool /
// TrySubmitThreadpoolCallback / timer + wait callbacks) rather than hand-
// rolling one on top of std::thread. Most people reach for the latter, which
// is fine, but the OS-managed pool gives you dynamic sizing, work
// coalescing, and integrated timers/waits for free — worth knowing it
// exists.
//
// Author: (your name)

#pragma once

#include <windows.h>
#include <functional>
#include <memory>

namespace wsk {

class ThreadPool {
public:
    // minThreads/maxThreads follow the same semantics as
    // SetThreadpoolThreadMinimum/Maximum: the pool won't shrink below min
    // even when idle, and won't grow past max no matter the queue depth.
    explicit ThreadPool(DWORD minThreads = 2, DWORD maxThreads = 8);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Queues work onto the pool. Returns false if the submission itself
    // failed (rare — usually means the pool environment got torn down);
    // the work item's own exceptions are the caller's problem, same as any
    // fire-and-forget task queue.
    bool Submit(std::function<void()> work);

    // Schedules work to run once after dueTimeMs, using the pool's
    // integrated timer rather than a sleeping thread. This is the detail
    // that tends to separate "used std::thread + sleep" from "actually knows
    // the platform's timer facilities."
    bool SubmitDelayed(std::function<void()> work, DWORD dueTimeMs);

    // Blocks until every currently-queued (non-delayed) work item has
    // completed. Delayed items still pending are not waited on.
    void WaitForPendingWork();

private:
    static void CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE instance, void* context, PTP_WORK work);
    static void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE instance, void* context, PTP_TIMER timer);

    PTP_POOL pool_ = nullptr;
    TP_CALLBACK_ENVIRON environ_{};
    PTP_CLEANUP_GROUP cleanupGroup_ = nullptr;
};

} // namespace wsk
