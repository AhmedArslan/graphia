#include "deferredexecutor.h"

#include "shared/utils/thread.h"
#include "shared/utils/container.h"

#include <QDebug>
#include <QtGlobal>

DeferredExecutor::DeferredExecutor()
{
    _debug = qEnvironmentVariableIntValue("DEFERREDEXECUTOR_DEBUG");
}

DeferredExecutor::~DeferredExecutor()
{
    cancel();
}

size_t DeferredExecutor::enqueue(TaskFn&& function, const QString& description)
{
    std::unique_lock<std::recursive_mutex> lock(_mutex);

    Task task;
    task._function = std::move(function);
    task._description = description;

    if(_debug > 1)
        qDebug() << "enqueue(...) thread:" << u::currentThreadName() << description;

    _tasks.emplace_back(task);

    return _tasks.size();
}

void DeferredExecutor::execute()
{
    std::unique_lock<std::recursive_mutex> lock(_mutex);

    if(_paused)
        return;

    if(!_tasks.empty() && _debug > 0)
    {
        qDebug() << "execute() thread" << u::currentThreadName();

        for(auto& task : _tasks)
            qDebug() << "\t" << task._description;
    }

    while(!_tasks.empty())
        executeOne();
}

void DeferredExecutor::executeOne()
{
    std::unique_lock<std::recursive_mutex> lock(_mutex);

    if(_paused)
        return;

    auto task = _tasks.front();
    _tasks.pop_front();

    if(_debug > 2)
        qDebug() << "Executing" << task._description;

    task._function();

    // Decrement all the wait counts
    for(auto it = _waitCount.begin(); it != _waitCount.end();)
    {
        if(it->second > 0)
            it->second--;

        if(it->second == 0)
            it = _waitCount.erase(it);
        else
            ++it;
    }

    _waitCondition.notify_all();
}

void DeferredExecutor::cancel()
{
    std::unique_lock<std::recursive_mutex> lock(_mutex);

    while(!_tasks.empty())
        _tasks.pop_front();
}

void DeferredExecutor::pause()
{
    std::unique_lock<std::recursive_mutex> lock(_mutex);
    _paused = true;
}

void DeferredExecutor::resume()
{
    std::unique_lock<std::recursive_mutex> lock(_mutex);
    _paused = false;
}

bool DeferredExecutor::hasTasks() const
{
    std::unique_lock<std::recursive_mutex> lock(_mutex);

    return !_tasks.empty();
}

void DeferredExecutor::waitFor(size_t numTasks)
{
    std::unique_lock<std::recursive_mutex> lock(_mutex);

    numTasks = std::min(_tasks.size(), numTasks);
    auto threadId = std::this_thread::get_id();
    _waitCount[threadId] = numTasks;

    if(_debug > 1)
    {
        qDebug() << "waitFor(" << numTasks << ") thread:" <<
            u::currentThreadName();
    }

    // Keep waiting until there are no remaining tasks
    _waitCondition.wait(lock,
    [this, threadId]
    {
        return !u::contains(_waitCount, threadId);
    });

    if(_debug > 1)
    {
        qDebug() << "waitFor complete thread:" <<
            u::currentThreadName();
    }
}
