// thread_pool.cpp
//
// The main gotcha with the native thread pool API is lifetime management —
// PTP_WORK/PTP_TIMER objects and the context you hand them need to outlive
// the callback firing, but you also don't want to leak them if Submit() is
// called a thousand times. We solve that here by heap-allocating a small
// context struct per submission and letting the callback itself free it —
// the callback is, by construction, the last thing that touches it.

#include "thread_pool.h"
#include <stdexcept>
#include <string>

namespace wsk {

namespace {
struct WorkContext {
    std::function<void()> fn;
};
} // namespace

ThreadPool::ThreadPool(DWORD minThreads, DWORD maxThreads)
{
    pool_ = CreateThreadpool(nullptr);
    if (!pool_) {
        throw std::runtime_error("CreateThreadpool failed, error " +
                                  std::to_string(GetLastError()));
    }

    SetThreadpoolThreadMinimum(pool_, minThreads);
    SetThreadpoolThreadMaximum(pool_, maxThreads);

    cleanupGroup_ = CreateThreadpoolCleanupGroup();
    if (!cleanupGroup_) {
        CloseThreadpool(pool_);
        throw std::runtime_error("CreateThreadpoolCleanupGroup failed, error " +
                                  std::to_string(GetLastError()));
    }

    InitializeThreadpoolEnvironment(&environ_);
    SetThreadpoolCallbackPool(&environ_, pool_);
    SetThreadpoolCallbackCleanupGroup(&environ_, cleanupGroup_, nullptr);
}

ThreadPool::~ThreadPool()
{
    if (cleanupGroup_) {
        // fCancelPendingCallbacks=FALSE: let anything already queued finish
        // rather than dropping it on the floor during teardown. This is a
        // deliberate choice — a destructor silently discarding queued work
        // is a surprising and hard-to-debug behavior for callers.
        CloseThreadpoolCleanupGroupMembers(cleanupGroup_, FALSE, nullptr);
        CloseThreadpoolCleanupGroup(cleanupGroup_);
    }
    DestroyThreadpoolEnvironment(&environ_);
    if (pool_) {
        CloseThreadpool(pool_);
    }
}

void CALLBACK ThreadPool::WorkCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK work)
{
    std::unique_ptr<WorkContext> ctx(static_cast<WorkContext*>(context));
    ctx->fn();
    CloseThreadpoolWork(work);
}

void CALLBACK ThreadPool::TimerCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER timer)
{
    std::unique_ptr<WorkContext> ctx(static_cast<WorkContext*>(context));
    ctx->fn();
    CloseThreadpoolTimer(timer);
}

bool ThreadPool::Submit(std::function<void()> work)
{
    auto* ctx = new WorkContext{std::move(work)};

    PTP_WORK item = CreateThreadpoolWork(WorkCallback, ctx, &environ_);
    if (!item) {
        delete ctx;
        return false;
    }

    SubmitThreadpoolWork(item);
    return true;
}

bool ThreadPool::SubmitDelayed(std::function<void()> work, DWORD dueTimeMs)
{
    auto* ctx = new WorkContext{std::move(work)};

    PTP_TIMER timer = CreateThreadpoolTimer(TimerCallback, ctx, &environ_);
    if (!timer) {
        delete ctx;
        return false;
    }

    // FILETIME due times are negative for relative intervals, in 100ns
    // units — one of those API quirks you only remember because you've been
    // bitten by the sign once already.
    ULARGE_INTEGER due{};
    due.QuadPart = static_cast<ULONGLONG>(-1) * (static_cast<ULONGLONG>(dueTimeMs) * 10000ULL);

    FILETIME ft{};
    ft.dwLowDateTime = due.LowPart;
    ft.dwHighDateTime = due.HighPart;

    SetThreadpoolTimer(timer, &ft, 0, 0);
    return true;
}

void ThreadPool::WaitForPendingWork()
{
    // fCancelPendingCallbacks=FALSE + this call blocks until every
    // outstanding member of the cleanup group has run. It's a slightly odd
    // way to get a "barrier," but it's the one the API actually gives you
    // without hand-rolling a counting semaphore around every Submit().
    if (cleanupGroup_) {
        CloseThreadpoolCleanupGroupMembers(cleanupGroup_, FALSE, nullptr);
        CloseThreadpoolCleanupGroup(cleanupGroup_);

        cleanupGroup_ = CreateThreadpoolCleanupGroup();
        SetThreadpoolCallbackCleanupGroup(&environ_, cleanupGroup_, nullptr);
    }
}

} // namespace wsk
