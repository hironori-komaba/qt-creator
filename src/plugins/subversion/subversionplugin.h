/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#pragma once

#include "subversionsettings.h"

#include <vcsbase/vcsbaseplugin.h>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QDir;
class QAction;
class QTextCodec;
QT_END_NAMESPACE

namespace Core {
    class CommandLocator;
    class IEditor;
}
namespace Utils { class ParameterAction; }

namespace Subversion {
namespace Internal {

class SubversionSubmitEditor;
class SubversionControl;
class SubversionClient;

struct SubversionResponse
{
    bool error = false;
    QString stdOut;
    QString stdErr;
    QString message;
};

const char FileAddedC[]      = "A";
const char FileConflictedC[] = "C";
const char FileDeletedC[]    = "D";
const char FileModifiedC[]   = "M";

class SubversionPluginPrivate final : public VcsBase::VcsBasePluginPrivate
{
    Q_OBJECT

public:
    SubversionPluginPrivate();
    ~SubversionPluginPrivate() final;

    bool isVcsDirectory(const Utils::FilePath &fileName);

    SubversionClient *client() const;

    SubversionSubmitEditor *openSubversionSubmitEditor(const QString &fileName);

    // IVersionControl
    bool vcsAdd(const QString &workingDir, const QString &fileName);
    bool vcsDelete(const QString &workingDir, const QString &fileName);
    bool vcsMove(const QString &workingDir, const QString &from, const QString &to);
    bool managesDirectory(const QString &directory, QString *topLevel = nullptr) const;
    bool managesFile(const QString &workingDirectory, const QString &fileName) const;
    bool vcsCheckout(const QString &directory, const QByteArray &url);

    static SubversionPluginPrivate *instance();

    QString monitorFile(const QString &repository) const;
    QString synchronousTopic(const QString &repository) const;
    SubversionResponse runSvn(const QString &workingDir,
                              const QStringList &arguments, int timeOutS,
                              unsigned flags, QTextCodec *outputCodec = nullptr) const;
    void describe(const QString &source, const QString &changeNr);
    void vcsAnnotate(const QString &workingDir, const QString &file,
                     const QString &revision = QString(), int lineNumber = -1);

protected:
    void updateActions(VcsBase::VcsBasePluginPrivate::ActionState) override;
    bool submitEditorAboutToClose() override;

private:
    void addCurrentFile();
    void revertCurrentFile();
    void diffProject();
    void diffCurrentFile();
    void cleanCommitMessageFile();
    void startCommitAll();
    void startCommitProject();
    void startCommitCurrentFile();
    void revertAll();
    void filelogCurrentFile();
    void annotateCurrentFile();
    void projectStatus();
    void slotDescribe();
    void updateProject();
    void commitFromEditor() override;
    void diffCommitFiles(const QStringList &);
    void logProject();
    void logRepository();
    void diffRepository();
    void statusRepository();
    void updateRepository();

    inline bool isCommitEditorOpen() const;
    Core::IEditor *showOutputInEditor(const QString &title, const QString &output,
                                      int editorType, const QString &source,
                                      QTextCodec *codec);

    void filelog(const QString &workingDir,
                 const QString &file = QString(),
                 bool enableAnnotationContextMenu = false);
    void svnStatus(const QString &workingDir, const QString &relativePath = QString());
    void svnUpdate(const QString &workingDir, const QString &relativePath = QString());
    bool checkSVNSubDir(const QDir &directory) const;
    void startCommit(const QString &workingDir, const QStringList &files = QStringList());
    inline SubversionControl *subVersionControl() const;

    const QStringList m_svnDirectories;

    SubversionSettings m_settings;
    SubversionClient *m_client = nullptr;
    QString m_commitMessageFileName;
    QString m_commitRepository;

    Core::CommandLocator *m_commandLocator = nullptr;
    Utils::ParameterAction *m_addAction = nullptr;
    Utils::ParameterAction *m_deleteAction = nullptr;
    Utils::ParameterAction *m_revertAction = nullptr;
    Utils::ParameterAction *m_diffProjectAction = nullptr;
    Utils::ParameterAction *m_diffCurrentAction = nullptr;
    Utils::ParameterAction *m_logProjectAction = nullptr;
    QAction *m_logRepositoryAction = nullptr;
    QAction *m_commitAllAction = nullptr;
    QAction *m_revertRepositoryAction = nullptr;
    QAction *m_diffRepositoryAction = nullptr;
    QAction *m_statusRepositoryAction = nullptr;
    QAction *m_updateRepositoryAction = nullptr;
    Utils::ParameterAction *m_commitCurrentAction = nullptr;
    Utils::ParameterAction *m_filelogCurrentAction = nullptr;
    Utils::ParameterAction *m_annotateCurrentAction = nullptr;
    Utils::ParameterAction *m_statusProjectAction = nullptr;
    Utils::ParameterAction *m_updateProjectAction = nullptr;
    Utils::ParameterAction *m_commitProjectAction = nullptr;
    QAction *m_describeAction = nullptr;

    QAction *m_menuAction = nullptr;
    bool m_submitActionTriggered = false;
};

class SubversionPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Subversion.json")

    ~SubversionPlugin() final;

    bool initialize(const QStringList &arguments, QString *errorMessage) final;
    void extensionsInitialized() final;

#ifdef WITH_TESTS
private slots:
    void testLogResolving();
#endif

};

} // namespace Subversion
} // namespace Internal
