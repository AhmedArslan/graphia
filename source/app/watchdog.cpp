#include "watchdog.h"

#include "shared/utils/fatalerror.h"
#include "shared/utils/thread.h"

#include "application.h"

// Disable warnings from Valgrind
#ifndef _MSC_VER
#include "thirdparty/gccdiagaware.h"
#ifdef GCC_DIAGNOSTIC_AWARE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include "thirdparty/valgrind/valgrind.h"
#else
#define RUNNING_ON_VALGRIND 0
#endif

#include <QDebug>
#include <QCoreApplication>
#include <QMessageBox>
#include <QProcess>

Watchdog::Watchdog()
{
    auto *worker = new WatchdogWorker;
    worker->moveToThread(&_thread);
    connect(&_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &Watchdog::reset, worker, &WatchdogWorker::onReset);
    _thread.start();
}

Watchdog::~Watchdog()
{
    _thread.quit();
    _thread.wait();
}

void WatchdogWorker::showWarning()
{
    QString messageBoxExe = Application::resolvedExe(QStringLiteral("MessageBox"));

    if(messageBoxExe.isEmpty())
    {
        qWarning() << "Couldn't resolve MessageBox executable";
        return;
    }

    QStringList arguments;
    arguments <<
        QStringLiteral("-title") << QStringLiteral("Error") <<
        QStringLiteral("-text") << QString(
            tr("%1 is not responding. System resources could be under pressure, "
               "so you may optionally wait in case a recovery occurs. "
               "Alternatively, please report a bug if you believe the "
               "freeze is as a result of a software problem."))
            .arg(QCoreApplication::instance()->applicationName()) <<
        QStringLiteral("-icon") << QStringLiteral("Critical") <<
        QStringLiteral("-button") << QStringLiteral("Wait:Reset") <<
        QStringLiteral("-button") << QStringLiteral("Close and Report Bug:Destructive") <<
        QStringLiteral("-defaultButton") << QStringLiteral("Wait");

    auto warningProcess = new QProcess(this);

    // Remove the warning if we recover in the mean time
    connect(this, &WatchdogWorker::reset, warningProcess, &QProcess::kill);

    connect(warningProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, &WatchdogWorker::onWarningProcessFinished);
    connect(warningProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        warningProcess, &WatchdogWorker::deleteLater);

    warningProcess->start(messageBoxExe, arguments);
}

void WatchdogWorker::onReset()
{
    _timeoutDuration = _defaultTimeoutDuration;
    startTimer();
}

void WatchdogWorker::startTimer()
{
    using namespace std::chrono;

    if(_timer == nullptr)
    {
        _timer = new QTimer(this);
        _timer->setSingleShot(true);
        connect(_timer, &QTimer::timeout, this, [=]
        {
            auto howLate = duration_cast<milliseconds>(clock_type::now() - _expectedExpiry);

            // QTimers are guaranteed to be accurate within 5%, so this should be generous enough
            auto lateThreshold = _timeoutDuration * 0.1;

            if(howLate > lateThreshold)
            {
                // If we're significantly late, then the watchdog thread itself has been paused
                // for some time, implying that the *entire* application has been paused, so our
                // detection of the freeze is probably incorrect and we should wait another interval
                startTimer();
                return;
            }

            // Don't bother doing anything when running under Valgrind
            if(RUNNING_ON_VALGRIND) // NOLINT
                return;

            qWarning() << "Watchdog timed out! Deadlock? "
                "Infinite loop? Resuming from a breakpoint?";

#ifndef _DEBUG
            showWarning();
#endif
        });

        u::setCurrentThreadName(QStringLiteral("WatchdogThread"));
    }

    emit reset();

    _expectedExpiry = clock_type::now() + _timeoutDuration;
    _timer->start(_timeoutDuration);
}

void WatchdogWorker::onWarningProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // Check for a sane exit code and status in case our warning has been killed (by us)
    if(exitCode >=0 && exitCode < QMessageBox::NRoles && exitStatus == QProcess::NormalExit)
    {
        if(exitCode == QMessageBox::DestructiveRole)
        {
            // Deliberately crash if the user chooses not to wait
            FATAL_ERROR(WatchdogTimedOut);
        }
        else
        {
            _timeoutDuration *= 2;
            startTimer();
        }
    }
}
