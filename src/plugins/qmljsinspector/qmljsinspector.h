/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
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
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#ifndef QMLJSINSPECTOR_H
#define QMLJSINSPECTOR_H

#include "qmljsprivateapi.h"

#include <coreplugin/basemode.h>
#include <qmlprojectmanager/qmlprojectrunconfiguration.h>

#include <qmljs/qmljsdocument.h>
#include <qmljs/parser/qmljsastfwd_p.h>

#include <QtGui/QAction>
#include <QtCore/QObject>

namespace ProjectExplorer {
    class Project;
    class Environment;
}

namespace Core {
    class IContext;
}

namespace Debugger {
    class DebuggerRunControl;
}

namespace QmlJS {
    class ModelManagerInterface;
}

QT_FORWARD_DECLARE_CLASS(QDockWidget)

namespace QmlJSInspector {
namespace Internal {

class ClientProxy;
class InspectorContext;
class ContextCrumblePath;
class QmlJSLiveTextPreview;

class Inspector : public QObject
{
    Q_OBJECT

public:
    enum DebugMode {
        StandaloneMode,
        CppProjectWithQmlEngines,
        QmlProjectWithCppPlugins
    };
public:
    Inspector(QObject *parent = 0);
    virtual ~Inspector();

    bool connectToViewer(); // using host, port from widgets

    // returns false if project is not debuggable.
    bool setDebugConfigurationDataFromProject(ProjectExplorer::Project *projectToDebug);
    void startQmlProjectDebugger();

    QDeclarativeDebugExpressionQuery *executeExpression(int objectDebugId, const QString &objectId,
                                                        const QString &propertyName, const QVariant &value);

    QDeclarativeDebugExpressionQuery *setBindingForObject(int objectDebugId, const QString &objectId,
                                                          const QString &propertyName, const QVariant &value,
                                                          bool isLiteralValue);
    static bool showExperimentalWarning();
    static void setShowExperimentalWarning(bool value);
    static Inspector *instance();

    // returns the project being currently debugged, or 0 if not debugging anything
    ProjectExplorer::Project *debugProject() const;
    void createDockWidgets();

signals:
    void statusMessage(const QString &text);
    void livePreviewActivated(bool isActivated);

public slots:
    void setSimpleDockWidgetArrangement();
    void reloadQmlViewer();
    void serverReloaded();
    void setApplyChangesToQmlObserver(bool applyChanges);

private slots:
    void gotoObjectReferenceDefinition(const QDeclarativeDebugObjectReference &obj);

    void pollInspector();

    void setSelectedItemsByObjectReference(QList<QDeclarativeDebugObjectReference> objectReferences);
    void changeSelectedItems(const QList<QDeclarativeDebugObjectReference> &objects);

    void connected(QDeclarativeEngineDebug *client);
    void updateEngineList();

    void disconnected();

    void removePreviewForEditor(Core::IEditor *newEditor);
    void createPreviewForEditor(Core::IEditor *newEditor);

    void disableLivePreview();
    void crumblePathElementClicked(int);

    void currentDebugProjectRemoved();

private:
    bool addQuotesForData(const QVariant &value) const;
    void resetViews();


    QmlJS::ModelManagerInterface *modelManager();
    void initializeDocuments();
    void applyChangesToQmlObserverHelper(bool applyChanges);

private:
    QWeakPointer<QDeclarativeEngineDebug> m_client;
    QmlProjectManager::QmlProjectRunConfigurationDebugData m_runConfigurationDebugData;
    InspectorContext *m_context;
    QTimer *m_connectionTimer;
    int m_connectionAttempts;
    ClientProxy *m_clientProxy;

    static bool m_showExperimentalWarning;
    bool m_listeningToEditorManager;

    ContextCrumblePath *m_crumblePath;
    QDockWidget *m_crumblePathDock;

    // Qml/JS integration
    QHash<QString, QmlJSLiveTextPreview *> m_textPreviews;
    QmlJS::Snapshot m_loadedSnapshot; //the snapshot loaded by the viewer
    ProjectExplorer::Project *m_debugProject;

    static Inspector *m_instance;
};

} // Internal
} // QmlJSInspector

#endif
