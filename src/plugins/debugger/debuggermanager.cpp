/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact:  Qt Software Information (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at qt-sales@nokia.com.
**
**************************************************************************/

#include "debuggermanager.h"

#include "debuggeractions.h"
#include "debuggerrunner.h"
#include "debuggerconstants.h"
#include "idebuggerengine.h"

#include "breakwindow.h"
#include "disassemblerwindow.h"
#include "debuggeroutputwindow.h"
#include "moduleswindow.h"
#include "registerwindow.h"
#include "stackwindow.h"
#include "sourcefileswindow.h"
#include "threadswindow.h"
#include "watchwindow.h"

#include "ui_breakbyfunction.h"

#include "disassemblerhandler.h"
#include "breakhandler.h"
#include "moduleshandler.h"
#include "registerhandler.h"
#include "stackhandler.h"
#include "watchhandler.h"

#include "debuggerdialogs.h"
#ifdef Q_OS_WIN
#  include "peutils.h"
#endif
#include <coreplugin/icore.h>
#include <utils/qtcassert.h>

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>
#include <QtCore/QTime>
#include <QtCore/QTimer>

#include <QtGui/QAction>
#include <QtGui/QComboBox>
#include <QtGui/QDockWidget>
#include <QtGui/QErrorMessage>
#include <QtGui/QFileDialog>
#include <QtGui/QLabel>
#include <QtGui/QMainWindow>
#include <QtGui/QMessageBox>
#include <QtGui/QPlainTextEdit>
#include <QtGui/QStatusBar>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QToolBar>
#include <QtGui/QToolButton>
#include <QtGui/QPushButton>
#include <QtGui/QToolTip>

using namespace Debugger;
using namespace Debugger::Internal;
using namespace Debugger::Constants;

static const QString tooltipIName = "tooltip";

#define _(s) QString::fromLatin1(s)

static const char *stateName(int s)
{
    switch (s) {
    case DebuggerProcessNotReady:
        return "DebuggerProcessNotReady";
    case DebuggerProcessStartingUp:
        return "DebuggerProcessStartingUp";
    case DebuggerInferiorRunningRequested:
        return "DebuggerInferiorRunningRequested";
    case DebuggerInferiorRunning:
        return "DebuggerInferiorRunning";
    case DebuggerInferiorStopRequested:
        return "DebuggerInferiorStopRequested";
    case DebuggerInferiorStopped:
        return "DebuggerInferiorStopped";
    }
    return "<unknown>";
}

///////////////////////////////////////////////////////////////////////
//
// BreakByFunctionDialog
//
///////////////////////////////////////////////////////////////////////

class BreakByFunctionDialog : public QDialog, Ui::BreakByFunctionDialog
{
    Q_OBJECT

public:
    explicit BreakByFunctionDialog(QWidget *parent)
      : QDialog(parent)
    {
        setupUi(this);
        connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
        connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
    }
    QString functionName() const { return functionLineEdit->text(); }
};


///////////////////////////////////////////////////////////////////////
//
// DebuggerManager
//
///////////////////////////////////////////////////////////////////////

static IDebuggerEngine *gdbEngine = 0;
static IDebuggerEngine *winEngine = 0;
static IDebuggerEngine *scriptEngine = 0;
static IDebuggerEngine *tcfEngine = 0;

// The creation functions take a list of options pages they can add to.
// This allows for having a "enabled" toggle on the page indepently
// of the engine.
IDebuggerEngine *createGdbEngine(DebuggerManager *parent, QList<Core::IOptionsPage*> *);
IDebuggerEngine *createWinEngine(DebuggerManager *, bool /* cmdLineDisabled */, QList<Core::IOptionsPage*> *)
#ifdef CDB_ENABLED
;
#else
{ return 0; }
#endif
IDebuggerEngine *createScriptEngine(DebuggerManager *parent, QList<Core::IOptionsPage*> *);
IDebuggerEngine *createTcfEngine(DebuggerManager *parent, QList<Core::IOptionsPage*> *);

DebuggerManager::DebuggerManager()
{
    init();
}

DebuggerManager::~DebuggerManager()
{
    #define doDelete(ptr) delete ptr; ptr = 0
    doDelete(gdbEngine);
    doDelete(winEngine);
    doDelete(scriptEngine);
    doDelete(tcfEngine);
    #undef doDelete
}

void DebuggerManager::init()
{
    m_status = -1;
    m_busy = false;

    m_attachedPID = 0;
    m_runControl = 0;

    m_disassemblerHandler = 0;
    m_modulesHandler = 0;
    m_registerHandler = 0;

    m_statusLabel = new QLabel;
    // FIXME: Do something to show overly long messages at least partially
    //QSizePolicy policy = m_statusLabel->sizePolicy();
    //policy.setHorizontalPolicy(QSizePolicy::MinimumExpanding);
    //m_statusLabel->setSizePolicy(policy);
    //m_statusLabel->setWordWrap(true);
    m_breakWindow = new BreakWindow;
    m_disassemblerWindow = new DisassemblerWindow;
    m_modulesWindow = new ModulesWindow(this);
    m_outputWindow = new DebuggerOutputWindow;
    m_registerWindow = new RegisterWindow;
    m_stackWindow = new StackWindow;
    m_sourceFilesWindow = new SourceFilesWindow;
    m_threadsWindow = new ThreadsWindow;
    m_localsWindow = new WatchWindow(WatchWindow::LocalsType);
    m_watchersWindow = new WatchWindow(WatchWindow::WatchersType);
    //m_tooltipWindow = new WatchWindow(WatchWindow::TooltipType);
    //m_watchersWindow = new QTreeView;
    m_tooltipWindow = new QTreeView;
    m_statusTimer = new QTimer(this);

    m_mainWindow = new QMainWindow;
    m_mainWindow->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    m_mainWindow->setDocumentMode(true);

    // Stack
    m_stackHandler = new StackHandler;
    QAbstractItemView *stackView =
        qobject_cast<QAbstractItemView *>(m_stackWindow);
    stackView->setModel(m_stackHandler->stackModel());
    connect(stackView, SIGNAL(frameActivated(int)),
        this, SLOT(activateFrame(int)));
    connect(theDebuggerAction(ExpandStack), SIGNAL(triggered()),
        this, SLOT(reloadFullStack()));
    connect(theDebuggerAction(MaximalStackDepth), SIGNAL(triggered()),
        this, SLOT(reloadFullStack()));

    // Threads
    m_threadsHandler = new ThreadsHandler;
    QAbstractItemView *threadsView =
        qobject_cast<QAbstractItemView *>(m_threadsWindow);
    threadsView->setModel(m_threadsHandler->threadsModel());
    connect(threadsView, SIGNAL(threadSelected(int)),
        this, SLOT(selectThread(int)));

    // Disassembler
    m_disassemblerHandler = new DisassemblerHandler;
    QAbstractItemView *disassemblerView =
        qobject_cast<QAbstractItemView *>(m_disassemblerWindow);
    disassemblerView->setModel(m_disassemblerHandler->model());
    connect(m_disassemblerWindow, SIGNAL(reloadDisassemblerRequested()),
            this, SLOT(reloadDisassembler()));

    // Breakpoints
    m_breakHandler = new BreakHandler;
    QAbstractItemView *breakView =
        qobject_cast<QAbstractItemView *>(m_breakWindow);
    breakView->setModel(m_breakHandler->model());
    connect(breakView, SIGNAL(breakpointActivated(int)),
        m_breakHandler, SLOT(activateBreakpoint(int)));
    connect(breakView, SIGNAL(breakpointDeleted(int)),
        m_breakHandler, SLOT(removeBreakpoint(int)));
    connect(breakView, SIGNAL(breakpointSynchronizationRequested()),
        this, SLOT(attemptBreakpointSynchronization()));
    connect(m_breakHandler, SIGNAL(gotoLocation(QString,int,bool)),
        this, SLOT(gotoLocation(QString,int,bool)));
    connect(m_breakHandler, SIGNAL(sessionValueRequested(QString,QVariant*)),
        this, SIGNAL(sessionValueRequested(QString,QVariant*)));
    connect(m_breakHandler, SIGNAL(setSessionValueRequested(QString,QVariant)),
        this, SIGNAL(setSessionValueRequested(QString,QVariant)));

    // Modules
    QAbstractItemView *modulesView =
        qobject_cast<QAbstractItemView *>(m_modulesWindow);
    m_modulesHandler = new ModulesHandler;
    modulesView->setModel(m_modulesHandler->model());
    connect(modulesView, SIGNAL(reloadModulesRequested()),
        this, SLOT(reloadModules()));
    connect(modulesView, SIGNAL(loadSymbolsRequested(QString)),
        this, SLOT(loadSymbols(QString)));
    connect(modulesView, SIGNAL(loadAllSymbolsRequested()),
        this, SLOT(loadAllSymbols()));
    connect(modulesView, SIGNAL(fileOpenRequested(QString)),
        this, SLOT(fileOpen(QString)));
    connect(modulesView, SIGNAL(newDockRequested(QWidget*)),
        this, SLOT(createNewDock(QWidget*)));

    // Source Files
    //m_sourceFilesHandler = new SourceFilesHandler;
    QAbstractItemView *sourceFilesView =
        qobject_cast<QAbstractItemView *>(m_sourceFilesWindow);
    //sourceFileView->setModel(m_stackHandler->stackModel());
    connect(sourceFilesView, SIGNAL(reloadSourceFilesRequested()),
        this, SLOT(reloadSourceFiles()));
    connect(sourceFilesView, SIGNAL(fileOpenRequested(QString)),
        this, SLOT(fileOpen(QString)));

    // Registers 
    QAbstractItemView *registerView =
        qobject_cast<QAbstractItemView *>(m_registerWindow);
    m_registerHandler = new RegisterHandler;
    registerView->setModel(m_registerHandler->model());

    // Locals
    m_watchHandler = new WatchHandler;
    QTreeView *localsView = qobject_cast<QTreeView *>(m_localsWindow);
    localsView->setModel(m_watchHandler->model());

    // Watchers 
    QTreeView *watchersView = qobject_cast<QTreeView *>(m_watchersWindow);
    watchersView->setModel(m_watchHandler->model());
    connect(m_watchHandler, SIGNAL(sessionValueRequested(QString,QVariant*)),
        this, SIGNAL(sessionValueRequested(QString,QVariant*)));
    connect(m_watchHandler, SIGNAL(setSessionValueRequested(QString,QVariant)),
        this, SIGNAL(setSessionValueRequested(QString,QVariant)));
    connect(theDebuggerAction(AssignValue), SIGNAL(triggered()),
        this, SLOT(assignValueInDebugger()), Qt::QueuedConnection);

    // Tooltip
    QTreeView *tooltipView = qobject_cast<QTreeView *>(m_tooltipWindow);
    tooltipView->setModel(m_watchHandler->model());

    connect(m_watchHandler, SIGNAL(watchModelUpdateRequested()),
        this, SLOT(updateWatchModel()));

    m_continueAction = new QAction(this);
    m_continueAction->setText(tr("Continue"));
    m_continueAction->setIcon(QIcon(":/gdbdebugger/images/debugger_continue_small.png"));

    m_stopAction = new QAction(this);
    m_stopAction->setText(tr("Interrupt"));
    m_stopAction->setIcon(QIcon(":/gdbdebugger/images/debugger_interrupt_small.png"));

    m_resetAction = new QAction(this);
    m_resetAction->setText(tr("Reset Debugger"));

    m_nextAction = new QAction(this);
    m_nextAction->setText(tr("Step Over"));
    //m_nextAction->setShortcut(QKeySequence(tr("F6")));
    m_nextAction->setIcon(QIcon(":/gdbdebugger/images/debugger_stepover_small.png"));

    m_stepAction = new QAction(this);
    m_stepAction->setText(tr("Step Into"));
    //m_stepAction->setShortcut(QKeySequence(tr("F7")));
    m_stepAction->setIcon(QIcon(":/gdbdebugger/images/debugger_stepinto_small.png"));

    m_nextIAction = new QAction(this);
    m_nextIAction->setText(tr("Step Over Instruction"));
    //m_nextIAction->setShortcut(QKeySequence(tr("Shift+F6")));
    m_nextIAction->setIcon(QIcon(":/gdbdebugger/images/debugger_stepoverproc_small.png"));

    m_stepIAction = new QAction(this);
    m_stepIAction->setText(tr("Step One Instruction"));
    //m_stepIAction->setShortcut(QKeySequence(tr("Shift+F9")));
    m_stepIAction->setIcon(QIcon(":/gdbdebugger/images/debugger_steponeproc_small.png"));

    m_stepOutAction = new QAction(this);
    m_stepOutAction->setText(tr("Step Out"));
    //m_stepOutAction->setShortcut(QKeySequence(tr("Shift+F7")));
    m_stepOutAction->setIcon(QIcon(":/gdbdebugger/images/debugger_stepout_small.png"));

    m_runToLineAction = new QAction(this);
    m_runToLineAction->setText(tr("Run to Line"));

    m_runToFunctionAction = new QAction(this);
    m_runToFunctionAction->setText(tr("Run to Outermost Function"));

    m_jumpToLineAction = new QAction(this);
    m_jumpToLineAction->setText(tr("Jump to Line"));

    m_breakAction = new QAction(this);
    m_breakAction->setText(tr("Toggle Breakpoint"));

    m_breakByFunctionAction = new QAction(this);
    m_breakByFunctionAction->setText(tr("Set Breakpoint at Function..."));

    m_breakAtMainAction = new QAction(this);
    m_breakAtMainAction->setText(tr("Set Breakpoint at Function \"main\""));

    m_watchAction = new QAction(this);
    m_watchAction->setText(tr("Add to Watch Window"));

    // For usuage hints oin focus{In,Out}
    connect(m_continueAction, SIGNAL(triggered()),
        this, SLOT(continueExec()));

    connect(m_stopAction, SIGNAL(triggered()),
        this, SLOT(interruptDebuggingRequest()));
    connect(m_resetAction, SIGNAL(triggered()),
        this, SLOT(exitDebugger()));
    connect(m_nextAction, SIGNAL(triggered()),
        this, SLOT(nextExec()));
    connect(m_stepAction, SIGNAL(triggered()),
        this, SLOT(stepExec()));
    connect(m_nextIAction, SIGNAL(triggered()),
        this, SLOT(nextIExec()));
    connect(m_stepIAction, SIGNAL(triggered()),
        this, SLOT(stepIExec()));
    connect(m_stepOutAction, SIGNAL(triggered()),
        this, SLOT(stepOutExec()));
    connect(m_runToLineAction, SIGNAL(triggered()),
        this, SLOT(runToLineExec()));
    connect(m_runToFunctionAction, SIGNAL(triggered()),
        this, SLOT(runToFunctionExec()));
    connect(m_jumpToLineAction, SIGNAL(triggered()),
        this, SLOT(jumpToLineExec()));
    connect(m_watchAction, SIGNAL(triggered()),
        this, SLOT(addToWatchWindow()));
    connect(m_breakAction, SIGNAL(triggered()),
        this, SLOT(toggleBreakpoint()));
    connect(m_breakByFunctionAction, SIGNAL(triggered()),
        this, SLOT(breakByFunction()));
    connect(m_breakAtMainAction, SIGNAL(triggered()),
        this, SLOT(breakAtMain()));

    connect(m_statusTimer, SIGNAL(timeout()),
        this, SLOT(clearStatusMessage()));

    connect(theDebuggerAction(ExecuteCommand), SIGNAL(triggered()),
        this, SLOT(executeDebuggerCommand()));


    m_breakDock = createDockForWidget(m_breakWindow);

    m_disassemblerDock = createDockForWidget(m_disassemblerWindow);
    connect(m_disassemblerDock->toggleViewAction(), SIGNAL(toggled(bool)),
        this, SLOT(reloadDisassembler()), Qt::QueuedConnection);

    m_modulesDock = createDockForWidget(m_modulesWindow);
    connect(m_modulesDock->toggleViewAction(), SIGNAL(toggled(bool)),
        this, SLOT(reloadModules()), Qt::QueuedConnection);

    m_registerDock = createDockForWidget(m_registerWindow);
    connect(m_registerDock->toggleViewAction(), SIGNAL(toggled(bool)),
        this, SLOT(reloadRegisters()), Qt::QueuedConnection);

    m_outputDock = createDockForWidget(m_outputWindow);

    m_stackDock = createDockForWidget(m_stackWindow);

    m_sourceFilesDock = createDockForWidget(m_sourceFilesWindow);
    connect(m_sourceFilesDock->toggleViewAction(), SIGNAL(toggled(bool)),
        this, SLOT(reloadSourceFiles()), Qt::QueuedConnection);

    m_threadsDock = createDockForWidget(m_threadsWindow);

    setStatus(DebuggerProcessNotReady);
}

QList<Core::IOptionsPage*> DebuggerManager::initializeEngines(const QStringList &arguments)
{
    QList<Core::IOptionsPage*> rc;
    gdbEngine = createGdbEngine(this, &rc);
    const bool cdbDisabled = arguments.contains(_("-disable-cdb"));
    winEngine = createWinEngine(this, cdbDisabled, &rc);
    scriptEngine = createScriptEngine(this, &rc);
    tcfEngine = createTcfEngine(this, &rc);
    m_engine = 0;
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << gdbEngine << winEngine << scriptEngine << rc.size();
    return rc;
}

IDebuggerEngine *DebuggerManager::engine()
{
    return m_engine;
}

IDebuggerManagerAccessForEngines *DebuggerManager::engineInterface()
{
    return dynamic_cast<IDebuggerManagerAccessForEngines *>(this);
}

void DebuggerManager::createDockWidgets()
{
    QSplitter *localsAndWatchers = new QSplitter(Qt::Vertical, 0);
    localsAndWatchers->setWindowTitle(m_localsWindow->windowTitle());
    localsAndWatchers->addWidget(m_localsWindow);
    localsAndWatchers->addWidget(m_watchersWindow);
    localsAndWatchers->setStretchFactor(0, 3);
    localsAndWatchers->setStretchFactor(1, 1);
    m_watchDock = createDockForWidget(localsAndWatchers);
}

void DebuggerManager::createNewDock(QWidget *widget)
{
    QDockWidget *dockWidget = new QDockWidget(widget->windowTitle(), m_mainWindow);
    dockWidget->setObjectName(widget->windowTitle());
    dockWidget->setFeatures(QDockWidget::DockWidgetClosable);
    dockWidget->setWidget(widget);
    m_mainWindow->addDockWidget(Qt::TopDockWidgetArea, dockWidget);
    dockWidget->show();
}

QDockWidget *DebuggerManager::createDockForWidget(QWidget *widget)
{
    QDockWidget *dockWidget = new QDockWidget(widget->windowTitle(), m_mainWindow);
    dockWidget->setObjectName(widget->windowTitle());
    dockWidget->setFeatures(QDockWidget::DockWidgetClosable);
    dockWidget->setTitleBarWidget(new QWidget(dockWidget));
    dockWidget->setWidget(widget);
    connect(dockWidget->toggleViewAction(), SIGNAL(toggled(bool)),
        this, SLOT(dockToggled(bool)), Qt::QueuedConnection);
    m_dockWidgets.append(dockWidget);
    return dockWidget;
}

void DebuggerManager::setSimpleDockWidgetArrangement()
{
    foreach (QDockWidget *dockWidget, m_dockWidgets)
        m_mainWindow->removeDockWidget(dockWidget);

    foreach (QDockWidget *dockWidget, m_dockWidgets) {
        m_mainWindow->addDockWidget(Qt::BottomDockWidgetArea, dockWidget);
        dockWidget->show();
    }

    m_mainWindow->tabifyDockWidget(m_watchDock, m_breakDock);
    m_mainWindow->tabifyDockWidget(m_watchDock, m_disassemblerDock);
    m_mainWindow->tabifyDockWidget(m_watchDock, m_modulesDock);
    m_mainWindow->tabifyDockWidget(m_watchDock, m_outputDock);
    m_mainWindow->tabifyDockWidget(m_watchDock, m_registerDock);
    m_mainWindow->tabifyDockWidget(m_watchDock, m_threadsDock);
    m_mainWindow->tabifyDockWidget(m_watchDock, m_sourceFilesDock);

    // They following views are rarely used in ordinary debugging. Hiding them
    // saves cycles since the corresponding information won't be retrieved.
    m_sourceFilesDock->hide();
    m_registerDock->hide();
    m_disassemblerDock->hide();
    m_modulesDock->hide();
    m_outputDock->hide();
}

void DebuggerManager::setLocked(bool locked)
{
    const QDockWidget::DockWidgetFeatures features =
            (locked) ? QDockWidget::DockWidgetClosable :
                       QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable;

    foreach (QDockWidget *dockWidget, m_dockWidgets) {
        QWidget *titleBarWidget = dockWidget->titleBarWidget();
        if (locked && !titleBarWidget)
            titleBarWidget = new QWidget(dockWidget);
        else if (!locked && titleBarWidget) {
            delete titleBarWidget;
            titleBarWidget = 0;
        }
        dockWidget->setTitleBarWidget(titleBarWidget);
        dockWidget->setFeatures(features);
    }
}

void DebuggerManager::dockToggled(bool on)
{
    QDockWidget *dw = qobject_cast<QDockWidget *>(sender()->parent());
    if (on && dw)
        dw->raise();
}

QAbstractItemModel *DebuggerManager::threadsModel()
{
    return qobject_cast<ThreadsWindow*>(m_threadsWindow)->model();
}

void DebuggerManager::clearStatusMessage()
{
    m_statusLabel->setText(m_lastPermanentStatusMessage);
}

void DebuggerManager::showStatusMessage(const QString &msg, int timeout)
{
    Q_UNUSED(timeout)
    if (Debugger::Constants::Internal::debug)
        qDebug() << "STATUS MSG: " << msg;
    showDebuggerOutput("status:", msg);
    m_statusLabel->setText("   " + msg);
    if (timeout > 0) {
        m_statusTimer->setSingleShot(true);
        m_statusTimer->start(timeout);
    } else {
        m_lastPermanentStatusMessage = msg;
        m_statusTimer->stop();
    }
}

void DebuggerManager::notifyInferiorStopRequested()
{
    setStatus(DebuggerInferiorStopRequested);
    showStatusMessage(tr("Stop requested..."), 5000);
}

void DebuggerManager::notifyInferiorStopped()
{
    resetLocation();
    setStatus(DebuggerInferiorStopped);
    showStatusMessage(tr("Stopped."), 5000);
}

void DebuggerManager::notifyInferiorRunningRequested()
{
    setStatus(DebuggerInferiorRunningRequested);
    showStatusMessage(tr("Running requested..."), 5000);
}

void DebuggerManager::notifyInferiorRunning()
{
    setStatus(DebuggerInferiorRunning);
    showStatusMessage(tr("Running..."), 5000);
}

void DebuggerManager::notifyInferiorExited()
{
    setStatus(DebuggerProcessNotReady);
    showStatusMessage(tr("Stopped."), 5000);
}

void DebuggerManager::notifyInferiorPidChanged(int pid)
{
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << pid;
    //QMessageBox::warning(0, "PID", "PID: " + QString::number(pid)); 
    emit inferiorPidChanged(pid);
}

void DebuggerManager::showApplicationOutput(const QString &str)
{
     emit applicationOutputAvailable(str);
}

void DebuggerManager::shutdown()
{
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << m_engine;

    if (m_engine)
        m_engine->shutdown();
    m_engine = 0;

    #define doDelete(ptr) delete ptr; ptr = 0
    doDelete(scriptEngine);
    doDelete(gdbEngine);
    doDelete(winEngine);
    doDelete(tcfEngine);

    // Delete these manually before deleting the manager
    // (who will delete the models for most views)
    doDelete(m_breakWindow);
    doDelete(m_disassemblerWindow);
    doDelete(m_modulesWindow);
    doDelete(m_outputWindow);
    doDelete(m_registerWindow);
    doDelete(m_stackWindow);
    doDelete(m_sourceFilesWindow);
    doDelete(m_threadsWindow);
    doDelete(m_tooltipWindow);
    doDelete(m_watchersWindow);
    doDelete(m_localsWindow);

    doDelete(m_breakHandler);
    doDelete(m_disassemblerHandler);
    doDelete(m_threadsHandler);
    doDelete(m_modulesHandler);
    doDelete(m_registerHandler);
    doDelete(m_stackHandler);
    doDelete(m_watchHandler);
    //qDebug() << "DEBUGGER_MANAGER SHUTDOWN END";
    #undef doDelete
}

BreakpointData *DebuggerManager::findBreakpoint(const QString &fileName, int lineNumber)
{
    if (!m_breakHandler)
        return 0;
    int index = m_breakHandler->findBreakpoint(fileName, lineNumber);
    return index == -1 ? 0 : m_breakHandler->at(index);
}

void DebuggerManager::toggleBreakpoint()
{
    QString fileName;
    int lineNumber = -1;
    queryCurrentTextEditor(&fileName, &lineNumber, 0);
    if (lineNumber == -1)
        return;
    toggleBreakpoint(fileName, lineNumber);
}

void DebuggerManager::toggleBreakpoint(const QString &fileName, int lineNumber)
{
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << fileName << lineNumber;

    QTC_ASSERT(m_breakHandler, return);
    if (status() != DebuggerInferiorRunning
         && status() != DebuggerInferiorStopped 
         && status() != DebuggerProcessNotReady) {
        showStatusMessage(tr("Changing breakpoint state requires either a "
            "fully running or fully stopped application."));
        return;
    }

    int index = m_breakHandler->findBreakpoint(fileName, lineNumber);
    if (index == -1)
        m_breakHandler->setBreakpoint(fileName, lineNumber);
    else
        m_breakHandler->removeBreakpoint(index);
    
    attemptBreakpointSynchronization();
}

void DebuggerManager::toggleBreakpointEnabled(const QString &fileName, int lineNumber)
{
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << fileName << lineNumber;

    QTC_ASSERT(m_breakHandler, return);
    if (status() != DebuggerInferiorRunning
         && status() != DebuggerInferiorStopped 
         && status() != DebuggerProcessNotReady) {
        showStatusMessage(tr("Changing breakpoint state requires either a "
            "fully running or fully stopped application."));
        return;
    }

    m_breakHandler->toggleBreakpointEnabled(fileName, lineNumber);

    attemptBreakpointSynchronization();
}

void DebuggerManager::attemptBreakpointSynchronization()
{
    if (m_engine)
        m_engine->attemptBreakpointSynchronization();
}

void DebuggerManager::setToolTipExpression(const QPoint &pos, const QString &exp)
{
    if (m_engine)
        m_engine->setToolTipExpression(pos, exp);
}

void DebuggerManager::updateWatchModel()
{
    if (m_engine)
        m_engine->updateWatchModel();
}

QVariant DebuggerManager::sessionValue(const QString &name)
{
    // this is answered by the plugin
    QVariant value;
    emit sessionValueRequested(name, &value);
    return value;
}

QVariant DebuggerManager::configValue(const QString &name)
{
    // this is answered by the plugin
    QVariant value;
    emit configValueRequested(name, &value);
    return value;
}

void DebuggerManager::queryConfigValue(const QString &name, QVariant *value)
{
    // this is answered by the plugin
    emit configValueRequested(name, value);
}

void DebuggerManager::setConfigValue(const QString &name, const QVariant &value)
{
    // this is answered by the plugin
    emit setConfigValueRequested(name, value);
}

// Figure out the debugger type of an executable
static IDebuggerEngine *determineDebuggerEngine(const QString &executable,
                                  QString *errorMessage)
{
    if (executable.endsWith(_(".js")))
        return scriptEngine;

#ifndef Q_OS_WIN
    Q_UNUSED(errorMessage)
    return gdbEngine;
#else
    // If a file has PDB files, it has been compiled by VS.
    QStringList pdbFiles;
    if (!getPDBFiles(executable, &pdbFiles, errorMessage))
        return 0;
    if (pdbFiles.empty())
        return gdbEngine;

    // We need the CDB debugger in order to be able to debug VS
    // executables
    if (!winEngine) {
        *errorMessage = DebuggerManager::tr("Debugging VS executables is not supported.");
        return 0;
    }
    return winEngine;
#endif
}

// Figure out the debugger type of a PID
static IDebuggerEngine *determineDebuggerEngine(int  /* pid */,
                                  QString * /*errorMessage*/)
{
#ifdef Q_OS_WIN
    // Preferably Windows debugger
    return winEngine ? winEngine : gdbEngine;
#else
    return gdbEngine;
#endif
}

void DebuggerManager::startNewDebugger(DebuggerRunControl *runControl)
{
    m_runControl = runControl;

    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << startMode();

    // FIXME: Clean up

    switch  (startMode()) {
    case StartExternal: {
        StartExternalDialog dlg(mainWindow());
        dlg.setExecutableFile(
            configValue(_("LastExternalExecutableFile")).toString());
        dlg.setExecutableArguments(
            configValue(_("LastExternalExecutableArguments")).toString());
        if (dlg.exec() != QDialog::Accepted) {
            runControl->debuggingFinished();
            return;
        }
        setConfigValue(_("LastExternalExecutableFile"),
            dlg.executableFile());
        setConfigValue(_("LastExternalExecutableArguments"),
            dlg.executableArguments());
        m_executable = dlg.executableFile();
        m_processArgs = dlg.executableArguments().split(' ');
        m_workingDir = QString();
        m_attachedPID = -1;
        break;
    }
    case AttachExternal: {
        AttachExternalDialog dlg(mainWindow());
        if (dlg.exec() != QDialog::Accepted) {
            runControl->debuggingFinished();
            return;
        }
        m_executable = QString();
        m_processArgs = QStringList();
        m_workingDir = QString();
        m_attachedPID = dlg.attachPID();
        if (m_attachedPID == 0) {
            QMessageBox::warning(mainWindow(), tr("Warning"),
                tr("Cannot attach to PID 0"));
            runControl->debuggingFinished();
            return;
        }
        break;
    }
    case StartInternal: {
        if (m_executable.isEmpty()) {
            QString startDirectory = m_executable;
            if (m_executable.isEmpty()) {
                QString fileName;
                emit currentTextEditorRequested(&fileName, 0, 0);
                if (!fileName.isEmpty()) {
                    const QFileInfo editorFile(fileName);
                    startDirectory = editorFile.dir().absolutePath();
                }
            }
            StartExternalDialog dlg(mainWindow());
            dlg.setExecutableFile(startDirectory);
            if (dlg.exec() != QDialog::Accepted) {
                runControl->debuggingFinished();
                return;
            }
            m_executable = dlg.executableFile();
            m_processArgs = dlg.executableArguments().split(' ');
            m_workingDir = QString();
            m_attachedPID = 0;
        } else {
            //m_executable = QDir::convertSeparators(m_executable);
            //m_processArgs = sd.processArgs.join(_(" "));
            m_attachedPID = 0;
        }
        break;
    }
    case AttachCore: {
        AttachCoreDialog dlg(mainWindow());
        dlg.setExecutableFile(
            configValue(_("LastExternalExecutableFile")).toString());
        dlg.setCoreFile(
            configValue(_("LastExternalCoreFile")).toString());
        if (dlg.exec() != QDialog::Accepted) {
            runControl->debuggingFinished();
            return;
        }
        setConfigValue(_("LastExternalExecutableFile"),
            dlg.executableFile());
        setConfigValue(_("LastExternalCoreFile"),
            dlg.coreFile());
        m_executable = dlg.executableFile();
        m_coreFile = dlg.coreFile();
        m_processArgs.clear();
        m_workingDir = QString();
        m_attachedPID = -1;
        break;
    }
    case StartRemote: {
        StartRemoteDialog dlg(mainWindow());
        QStringList arches;
        arches.append(_("i386:x86-64:intel"));
        dlg.setRemoteArchitectures(arches);
        dlg.setRemoteChannel(
            configValue(_("LastRemoteChannel")).toString());
        dlg.setRemoteArchitecture(
            configValue(_("LastRemoteArchitecture")).toString());
        dlg.setServerStartScript(
            configValue(_("LastServerStartScript")).toString());
        dlg.setUseServerStartScript(
            configValue(_("LastUseServerStartScript")).toBool());
        if (dlg.exec() != QDialog::Accepted) {  
            runControl->debuggingFinished();
            return;
        }
        setConfigValue(_("LastRemoteChannel"), dlg.remoteChannel());
        setConfigValue(_("LastRemoteArchitecture"), dlg.remoteArchitecture());
        setConfigValue(_("LastServerStartScript"), dlg.serverStartScript());
        setConfigValue(_("LastUseServerStartScript"), dlg.useServerStartScript());
        m_remoteChannel = dlg.remoteChannel();
        m_remoteArchitecture = dlg.remoteArchitecture();
        m_serverStartScript = dlg.serverStartScript();
        if (!dlg.useServerStartScript())
            m_serverStartScript.clear();
        break;
    }
    case AttachTcf: {
        AttachTcfDialog dlg(mainWindow());
        QStringList arches;
        arches.append(_("i386:x86-64:intel"));
        dlg.setRemoteArchitectures(arches);
        dlg.setRemoteChannel(
            configValue(_("LastTcfRemoteChannel")).toString());
        dlg.setRemoteArchitecture(
            configValue(_("LastTcfRemoteArchitecture")).toString());
        dlg.setServerStartScript(
            configValue(_("LastTcfServerStartScript")).toString());
        dlg.setUseServerStartScript(
            configValue(_("LastTcfUseServerStartScript")).toBool());
        if (dlg.exec() != QDialog::Accepted) {  
            runControl->debuggingFinished();
            return;
        }
        setConfigValue(_("LastTcfRemoteChannel"), dlg.remoteChannel());
        setConfigValue(_("LastTcfRemoteArchitecture"), dlg.remoteArchitecture());
        setConfigValue(_("LastTcfServerStartScript"), dlg.serverStartScript());
        setConfigValue(_("LastTcfUseServerStartScript"), dlg.useServerStartScript());
        m_remoteChannel = dlg.remoteChannel();
        m_remoteArchitecture = dlg.remoteArchitecture();
        m_serverStartScript = dlg.serverStartScript();
        if (!dlg.useServerStartScript())
            m_serverStartScript.clear();
        break;
    }
    }

    emit debugModeRequested();

    QString errorMessage;
    if (startMode() == AttachExternal)
        m_engine = determineDebuggerEngine(m_attachedPID, &errorMessage);
    else if (startMode() == AttachTcf)
        m_engine = tcfEngine;
    else
        m_engine = determineDebuggerEngine(m_executable, &errorMessage);

    if (!m_engine) {
        QMessageBox::warning(mainWindow(), tr("Warning"),
                tr("Cannot debug '%1': %2").arg(m_executable, errorMessage));
        debuggingFinished();
        return;
    }
    if (Debugger::Constants::Internal::debug)
        qDebug() << m_executable << m_engine;

    setBusyCursor(false);
    setStatus(DebuggerProcessStartingUp);
    if (!m_engine->startDebugger()) {
        setStatus(DebuggerProcessNotReady);
        debuggingFinished();
        return;
    }

    return;
}

void DebuggerManager::cleanupViews()
{
    resetLocation();
    breakHandler()->setAllPending();
    stackHandler()->removeAll();
    threadsHandler()->removeAll();
    disassemblerHandler()->removeAll();
    modulesHandler()->removeAll();
    watchHandler()->cleanup();
    m_sourceFilesWindow->removeAll();
}

void DebuggerManager::exitDebugger()
{
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO;

    if (m_engine)
        m_engine->exitDebugger();
    cleanupViews();
    setStatus(DebuggerProcessNotReady);
    setBusyCursor(false);
    emit debuggingFinished();
}

void DebuggerManager::assignValueInDebugger()
{
    if (QAction *action = qobject_cast<QAction *>(sender())) {
        QString str = action->data().toString();
        int i = str.indexOf('=');
        if (i != -1)
            assignValueInDebugger(str.left(i), str.mid(i + 1));
    }
}

void DebuggerManager::assignValueInDebugger(const QString &expr, const QString &value)
{
    QTC_ASSERT(m_engine, return);
    m_engine->assignValueInDebugger(expr, value);
}

void DebuggerManager::activateFrame(int index)
{
    QTC_ASSERT(m_engine, return);
    m_engine->activateFrame(index);
}

void DebuggerManager::selectThread(int index)
{
    QTC_ASSERT(m_engine, return);
    m_engine->selectThread(index);
}

void DebuggerManager::loadAllSymbols()
{
    QTC_ASSERT(m_engine, return);
    m_engine->loadAllSymbols();
}

void DebuggerManager::loadSymbols(const QString &module)
{
    QTC_ASSERT(m_engine, return);
    m_engine->loadSymbols(module);
}

QList<Symbol> DebuggerManager::moduleSymbols(const QString &moduleName)
{
    QTC_ASSERT(m_engine, return QList<Symbol>());
    return m_engine->moduleSymbols(moduleName);
}

void DebuggerManager::stepExec()
{
    QTC_ASSERT(m_engine, return);
    resetLocation();
    m_engine->stepExec();
} 

void DebuggerManager::stepOutExec()
{
    QTC_ASSERT(m_engine, return);
    resetLocation();
    m_engine->stepOutExec();
}

void DebuggerManager::nextExec()
{
    QTC_ASSERT(m_engine, return);
    resetLocation();
    m_engine->nextExec();
}

void DebuggerManager::stepIExec()
{
    QTC_ASSERT(m_engine, return);
    resetLocation();
    m_engine->stepIExec();
}

void DebuggerManager::nextIExec()
{
    QTC_ASSERT(m_engine, return);
    resetLocation();
    m_engine->nextIExec();
}

void DebuggerManager::executeDebuggerCommand()
{
    if (QAction *action = qobject_cast<QAction *>(sender()))
        executeDebuggerCommand(action->data().toString());
}

void DebuggerManager::executeDebuggerCommand(const QString &command)
{
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO <<command;

    QTC_ASSERT(m_engine, return);
    m_engine->executeDebuggerCommand(command);
}

void DebuggerManager::sessionLoaded()
{
    cleanupViews();
    setStatus(DebuggerProcessNotReady);
    setBusyCursor(false);
    loadSessionData();
}

void DebuggerManager::sessionUnloaded()
{
    return;
    //FIXME: Breakview crashes on startup as there is 
    //cleanupViews();
    if (m_engine)
        m_engine->shutdown();
    setStatus(DebuggerProcessNotReady);
    setBusyCursor(false);
}

void DebuggerManager::aboutToSaveSession()
{
    saveSessionData();
}

void DebuggerManager::loadSessionData()
{
    m_breakHandler->loadSessionData();
    m_watchHandler->loadSessionData();
}

void DebuggerManager::saveSessionData()
{
    m_breakHandler->saveSessionData();
    m_watchHandler->saveSessionData();
}

void DebuggerManager::dumpLog()
{
    QString fileName = QFileDialog::getSaveFileName(mainWindow(),
        tr("Save Debugger Log"), QDir::tempPath());
    if (fileName.isEmpty())
        return;
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QTextStream ts(&file);      
    ts << m_outputWindow->inputContents();
    ts << "\n\n=======================================\n\n";
    ts << m_outputWindow->combinedContents();
}

void DebuggerManager::addToWatchWindow()
{
    // requires a selection, but that's the only case we want...
    QObject *ob = 0;
    queryCurrentTextEditor(0, 0, &ob);
    QPlainTextEdit *editor = qobject_cast<QPlainTextEdit*>(ob);
    if (!editor)
        return;
    QTextCursor tc = editor->textCursor();
    theDebuggerAction(WatchExpression)->setValue(tc.selectedText());
}

void DebuggerManager::setBreakpoint(const QString &fileName, int lineNumber)
{
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << fileName << lineNumber;

    QTC_ASSERT(m_breakHandler, return);
    m_breakHandler->setBreakpoint(fileName, lineNumber);
    attemptBreakpointSynchronization();
}

void DebuggerManager::breakByFunction(const QString &functionName)
{
    QTC_ASSERT(m_breakHandler, return);
    m_breakHandler->breakByFunction(functionName);
    attemptBreakpointSynchronization();
}

void DebuggerManager::breakByFunction()
{
    BreakByFunctionDialog dlg(m_mainWindow);
    if (dlg.exec()) 
        breakByFunction(dlg.functionName());
}

void DebuggerManager::breakAtMain()
{
#ifdef Q_OS_WIN
    breakByFunction("qMain");
#else
    breakByFunction("main");
#endif
}

static bool isAllowedTransition(int from, int to)
{
    return (from == -1)
      || (from == DebuggerProcessNotReady && to == DebuggerProcessStartingUp)
      //|| (from == DebuggerProcessStartingUp && to == DebuggerInferiorStopped)
      || (from == DebuggerInferiorStopped && to == DebuggerInferiorRunningRequested)
      || (from == DebuggerInferiorRunningRequested && to == DebuggerInferiorRunning)
      || (from == DebuggerInferiorRunning && to == DebuggerInferiorStopRequested)
      || (from == DebuggerInferiorRunning && to == DebuggerInferiorStopped)
      || (from == DebuggerInferiorStopRequested && to == DebuggerInferiorStopped)
      || (to == DebuggerProcessNotReady);  
}

void DebuggerManager::setStatus(int status)
{
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << "STATUS CHANGE: from" << stateName(m_status) << "to" << stateName(status);

    if (status == m_status)
        return;

    if (0 && !isAllowedTransition(m_status, status)) {
        const QString msg = QString::fromLatin1("%1: UNEXPECTED TRANSITION: %2 -> %3").
                            arg(_(Q_FUNC_INFO), _(stateName(m_status)), _(stateName(status)));
        qWarning("%s", qPrintable(msg));
    }

    m_status = status;

    const bool started = status == DebuggerInferiorRunning
        || status == DebuggerInferiorRunningRequested
        || status == DebuggerInferiorStopRequested
        || status == DebuggerInferiorStopped;

    //const bool starting = status == DebuggerProcessStartingUp;
    const bool running = status == DebuggerInferiorRunning;

    const bool ready = status == DebuggerInferiorStopped
            && startMode() != AttachCore;

    m_watchAction->setEnabled(ready);
    m_breakAction->setEnabled(true);

    bool interruptIsExit = !running;
    if (interruptIsExit) {
        m_stopAction->setIcon(QIcon(":/gdbdebugger/images/debugger_stop_small.png"));
        m_stopAction->setText(tr("Stop Debugger"));
    } else {
        m_stopAction->setIcon(QIcon(":/gdbdebugger/images/debugger_interrupt_small.png"));
        m_stopAction->setText(tr("Interrupt"));
    }

    m_stopAction->setEnabled(started);
    m_resetAction->setEnabled(true);

    m_stepAction->setEnabled(ready);
    m_stepOutAction->setEnabled(ready);
    m_runToLineAction->setEnabled(ready);
    m_runToFunctionAction->setEnabled(ready);
    m_jumpToLineAction->setEnabled(ready);
    m_nextAction->setEnabled(ready);
    m_stepIAction->setEnabled(ready);
    m_nextIAction->setEnabled(ready);
    //showStatusMessage(QString("started: %1, running: %2").arg(started).arg(running));
    emit statusChanged(m_status);
    const bool notbusy = ready || status == DebuggerProcessNotReady;
    setBusyCursor(!notbusy);
}

void DebuggerManager::setBusyCursor(bool busy)
{
    //qDebug() << "BUSY FROM: " << m_busy << " TO: " <<  m_busy;
    if (busy == m_busy)
        return;
    m_busy = busy;

    QCursor cursor(busy ? Qt::BusyCursor : Qt::ArrowCursor);
    m_breakWindow->setCursor(cursor);
    m_disassemblerWindow->setCursor(cursor);
    m_localsWindow->setCursor(cursor);
    m_modulesWindow->setCursor(cursor);
    m_outputWindow->setCursor(cursor);
    m_registerWindow->setCursor(cursor);
    m_stackWindow->setCursor(cursor);
    m_sourceFilesWindow->setCursor(cursor);
    m_threadsWindow->setCursor(cursor);
    m_tooltipWindow->setCursor(cursor);
    m_watchersWindow->setCursor(cursor);
}

void DebuggerManager::queryCurrentTextEditor(QString *fileName, int *lineNumber,
    QObject **object)
{
    emit currentTextEditorRequested(fileName, lineNumber, object);
}

void DebuggerManager::continueExec()
{
    if (m_engine)
        m_engine->continueInferior();
}

void DebuggerManager::detachDebugger()
{
    if (m_engine)
        m_engine->detachDebugger();
}

void DebuggerManager::interruptDebuggingRequest()
{
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << status();
    if (!m_engine)
        return;
    bool interruptIsExit = (status() != DebuggerInferiorRunning);
    if (interruptIsExit)
        exitDebugger();
    else {
        setStatus(DebuggerInferiorStopRequested);
        m_engine->interruptInferior();
    }
}

void DebuggerManager::runToLineExec()
{
    QString fileName;
    int lineNumber = -1;
    emit currentTextEditorRequested(&fileName, &lineNumber, 0);
    if (m_engine && !fileName.isEmpty()) {
        if (Debugger::Constants::Internal::debug)
            qDebug() << Q_FUNC_INFO << fileName << lineNumber;
        m_engine->runToLineExec(fileName, lineNumber);
    }
}

void DebuggerManager::runToFunctionExec()
{
    QString fileName;
    int lineNumber = -1;
    QObject *object = 0;
    emit currentTextEditorRequested(&fileName, &lineNumber, &object);
    QPlainTextEdit *ed = qobject_cast<QPlainTextEdit*>(object);
    if (!ed)
        return;
    QTextCursor cursor = ed->textCursor();
    QString functionName = cursor.selectedText();
    if (functionName.isEmpty()) {
        const QTextBlock block = cursor.block();
        const QString line = block.text();
        foreach (const QString &str, line.trimmed().split('(')) {
            QString a;
            for (int i = str.size(); --i >= 0; ) {
                if (!str.at(i).isLetterOrNumber())
                    break;
                a = str.at(i) + a;
            }
            if (!a.isEmpty()) {
                functionName = a;
                break;
            }
        }
    }
    if (Debugger::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << functionName;

    if (m_engine && !functionName.isEmpty())
        m_engine->runToFunctionExec(functionName);
}

void DebuggerManager::jumpToLineExec()
{
    QString fileName;
    int lineNumber = -1;
    emit currentTextEditorRequested(&fileName, &lineNumber, 0);
    if (m_engine && !fileName.isEmpty()) {
        if (Debugger::Constants::Internal::debug)
            qDebug() << Q_FUNC_INFO << fileName << lineNumber;
        m_engine->jumpToLineExec(fileName, lineNumber);
    }
}

void DebuggerManager::resetLocation()
{
    // connected to the plugin
    emit resetLocationRequested();
}

void DebuggerManager::gotoLocation(const QString &fileName, int line,
    bool setMarker)
{
    // connected to the plugin
    emit gotoLocationRequested(fileName, line, setMarker);
}

void DebuggerManager::fileOpen(const QString &fileName)
{
    // connected to the plugin
    emit gotoLocationRequested(fileName, 1, false);
}


//////////////////////////////////////////////////////////////////////
//
// Disassembler specific stuff
//
//////////////////////////////////////////////////////////////////////

void DebuggerManager::reloadDisassembler()
{
    if (m_engine && m_disassemblerDock && m_disassemblerDock->isVisible())
        m_engine->reloadDisassembler();
}

void DebuggerManager::disassemblerDockToggled(bool on)
{
    if (on)
        reloadDisassembler();
}


//////////////////////////////////////////////////////////////////////
//
// Source files specific stuff
//
//////////////////////////////////////////////////////////////////////

void DebuggerManager::reloadSourceFiles()
{
    if (m_engine && m_sourceFilesDock && m_sourceFilesDock->isVisible())
        m_engine->reloadSourceFiles();
}

void DebuggerManager::sourceFilesDockToggled(bool on)
{
    if (on)
        reloadSourceFiles();
}


//////////////////////////////////////////////////////////////////////
//
// Modules specific stuff
//
//////////////////////////////////////////////////////////////////////

void DebuggerManager::reloadModules()
{
    if (m_engine && m_modulesDock && m_modulesDock->isVisible())
        m_engine->reloadModules();
}

void DebuggerManager::modulesDockToggled(bool on)
{
    if (on)
        reloadModules();
}


//////////////////////////////////////////////////////////////////////
//
// Output specific stuff
//
//////////////////////////////////////////////////////////////////////

void DebuggerManager::showDebuggerOutput(const QString &prefix, const QString &msg)
{
    QTC_ASSERT(m_outputWindow, return);
    m_outputWindow->showOutput(prefix, msg);
}

void DebuggerManager::showDebuggerInput(const QString &prefix, const QString &msg)
{
    QTC_ASSERT(m_outputWindow, return);
    m_outputWindow->showInput(prefix, msg);
}


//////////////////////////////////////////////////////////////////////
//
// Register specific stuff
//
//////////////////////////////////////////////////////////////////////

void DebuggerManager::registerDockToggled(bool on)
{
    if (on)
        reloadRegisters();
}

void DebuggerManager::reloadRegisters()
{
    if (m_engine && m_registerDock && m_registerDock->isVisible())
        m_engine->reloadRegisters();
}

//////////////////////////////////////////////////////////////////////
//
// Dumpers. "Custom dumpers" are a library compiled against the current
// Qt containing functions to evaluate values of Qt classes
// (such as QString, taking pointers to their addresses).
// The library must be loaded into the debuggee.
//
//////////////////////////////////////////////////////////////////////

bool DebuggerManager::qtDumperLibraryEnabled() const
{
    return theDebuggerBoolSetting(UseDebuggingHelpers);
}

QString DebuggerManager::qtDumperLibraryName() const
{
    if (theDebuggerAction(UseCustomDebuggingHelperLocation)->value().toBool())
        return theDebuggerAction(CustomDebuggingHelperLocation)->value().toString();
    return m_dumperLib;
}

void DebuggerManager::showQtDumperLibraryWarning(const QString &details)
{
    QMessageBox dialog(mainWindow());
    QPushButton *qtPref = dialog.addButton(tr("Open Qt preferences"), QMessageBox::ActionRole);
    QPushButton *helperOff = dialog.addButton(tr("Turn helper usage off"), QMessageBox::ActionRole);
    QPushButton *justContinue = dialog.addButton(tr("Continue anyway"), QMessageBox::AcceptRole);
    dialog.setDefaultButton(justContinue);
    dialog.setWindowTitle(tr("Debugging helper missing"));
    dialog.setText(tr("The debugger did not find the debugging helper library."));
    dialog.setInformativeText(tr("The debugging helper is used to nicely format the values of Qt "
                                 "data types and some STL data types. "
                                 "It must be compiled for each Qt version which "
                                 "you can do in the Qt preferences page by selecting "
                                 "a Qt installation and clicking on 'Rebuild' for the debugging "
                                 "helper."));
    if (!details.isEmpty())
        dialog.setDetailedText(details);
    dialog.exec();
    if (dialog.clickedButton() == qtPref) {
        Core::ICore::instance()->showOptionsDialog(_("Qt4"), _("Qt Versions"));
    } else if (dialog.clickedButton() == helperOff) {
        theDebuggerAction(UseDebuggingHelpers)->setValue(qVariantFromValue(false), false);
    }
}

DebuggerStartMode DebuggerManager::startMode() const
{
    return m_runControl->startMode();
}

void DebuggerManager::reloadFullStack()
{
    if (m_engine)
        m_engine->reloadFullStack();
}


//////////////////////////////////////////////////////////////////////
//
// Testing
//
//////////////////////////////////////////////////////////////////////

void DebuggerManager::runTest(const QString &fileName)
{
    m_executable = fileName;
    m_processArgs = QStringList() << "--run-debuggee";
    m_workingDir = QString();
    //startNewDebugger(StartInternal);
}

#include "debuggermanager.moc"
