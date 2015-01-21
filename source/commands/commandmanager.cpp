#include "commandmanager.h"

#include "../utils/namethread.h"
#include "../utils/unique_lock_with_side_effects.h"

#include <thread>

CommandManager::CommandManager() :
    _lastExecutedIndex(-1),
    _busy(false)
{
    connect(this, &CommandManager::commandCompleted, this, &CommandManager::onCommandCompleted);
}

void CommandManager::executeReal(std::shared_ptr<Command> command)
{
    unique_lock_with_side_effects<std::mutex> lock(_mutex);
    auto commandPtr = command.get();

    lock.setPostUnlockAction([this, commandPtr] { _busy = false; emit commandCompleted(commandPtr, commandPtr->pastParticiple()); });
    command->setProgressFn([this, commandPtr](int progress) { emit commandProgress(commandPtr, progress); });

    auto executeCommand = [this](unique_lock_with_side_effects<std::mutex>&& lock, std::shared_ptr<Command> command)
    {
        nameCurrentThread(command->description());

        if(!command->execute())
        {
            lock.setPostUnlockAction([this] { _busy = false; emit commandCompleted(nullptr, QString()); });
            return;
        }

        // There are commands on the stack ahead of us; throw them away
        while(canRedoNoLocking())
            _stack.pop_back();

        _stack.push_back(command);
        _lastExecutedIndex = static_cast<int>(_stack.size()) - 1;
    };

    _busy = true;
    if(command->asynchronous())
    {
        emit commandWillExecuteAsynchronously(commandPtr, command->verb());
        _thread = std::thread(executeCommand, std::move(lock), command);
    }
    else
        executeCommand(std::move(lock), command);
}

void CommandManager::undo()
{
    unique_lock_with_side_effects<std::mutex> lock(_mutex);

    if(!canUndoNoLocking())
        return;

    auto command = _stack.at(_lastExecutedIndex);
    auto commandPtr = command.get();
    lock.setPostUnlockAction([this, commandPtr] { _busy = false; emit commandCompleted(commandPtr, QString()); });

    auto undoCommand = [this, command](unique_lock_with_side_effects<std::mutex>&& /*lock*/)
    {
        nameCurrentThread("(u) " + command->description());

        command->undo();
        _lastExecutedIndex--;
    };

    _busy = true;
    if(command->asynchronous())
    {
        emit commandWillExecuteAsynchronously(commandPtr, command->undoVerb());
        _thread = std::thread(undoCommand, std::move(lock));
    }
    else
        undoCommand(std::move(lock));
}

void CommandManager::redo()
{
    unique_lock_with_side_effects<std::mutex> lock(_mutex);

    if(!canRedoNoLocking())
        return;

    auto command = _stack.at(++_lastExecutedIndex);
    auto commandPtr = command.get();
    lock.setPostUnlockAction([this, commandPtr] { _busy = false; emit commandCompleted(commandPtr, commandPtr->pastParticiple()); });

    auto redoCommand = [this, command](unique_lock_with_side_effects<std::mutex>&& /*lock*/)
    {
        nameCurrentThread("(r) " + command->description());

        command->execute();
    };

    _busy = true;
    if(command->asynchronous())
    {
        emit commandWillExecuteAsynchronously(commandPtr, command->redoVerb());
        _thread = std::thread(redoCommand, std::move(lock));
    }
    else
        redoCommand(std::move(lock));
}

bool CommandManager::canUndo() const
{
    std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);

    if(lock.owns_lock())
        return canUndoNoLocking();

    return false;
}

bool CommandManager::canRedo() const
{
    std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);

    if(lock.owns_lock())
        return canRedoNoLocking();

    return false;
}

const std::vector<QString> CommandManager::undoableCommandDescriptions() const
{
    std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
    std::vector<QString> commandDescriptions;

    if(lock.owns_lock() && canUndoNoLocking())
    {
        for(int index = _lastExecutedIndex; index >= 0; index--)
            commandDescriptions.push_back(_stack.at(index)->description());
    }

    return commandDescriptions;
}

const std::vector<QString> CommandManager::redoableCommandDescriptions() const
{
    std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
    std::vector<QString> commandDescriptions;

    if(lock.owns_lock() && canRedoNoLocking())
    {
        for(int index = _lastExecutedIndex + 1; index < static_cast<int>(_stack.size()); index++)
            commandDescriptions.push_back(_stack.at(index)->description());
    }

    return commandDescriptions;
}

const QString CommandManager::nextUndoAction() const
{
    std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);

    if(lock.owns_lock() && canUndoNoLocking())
    {
        auto& command = _stack.at(_lastExecutedIndex);
        return command->undoDescription();
    }

    return QString();
}

const QString CommandManager::nextRedoAction() const
{
    std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);

    if(lock.owns_lock() && canRedoNoLocking())
    {
        auto& command = _stack.at(_lastExecutedIndex + 1);
        return command->redoDescription();
    }

    return QString();
}

bool CommandManager::busy() const
{
    return _busy;
}

bool CommandManager::canUndoNoLocking() const
{
    return _lastExecutedIndex >= 0;
}

bool CommandManager::canRedoNoLocking() const
{
    return _lastExecutedIndex < static_cast<int>(_stack.size()) - 1;
}

void CommandManager::onCommandCompleted(const Command*, const QString&)
{
    // If the command executed asynchronously, we need to join its thread
    if(_thread.joinable())
        _thread.join();
}
