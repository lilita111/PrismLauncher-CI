// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ModStoreDialog.h"
#include "ui_ModStoreDialog.h"

#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QMessageBox>

#include "Application.h"
#include "DesktopServices.h"
#include "minecraft/mod/ModStore.h"
#include "minecraft/mod/ModStoreTasks.h"
#include "ProgressDialog.h"

ModStoreDialog::ModStoreDialog(QWidget* parent) : QDialog(parent), ui(new Ui::ModStoreDialog)
{
    ui->setupUi(this);
    m_store = new ModStore(QDir(APPLICATION->dataRoot()).filePath("modstore"));
    refreshStats();
}

ModStoreDialog::~ModStoreDialog()
{
    delete ui;
    delete m_store;
}

void ModStoreDialog::refreshStats()
{
    bool ok = false;
    auto index = ModStore::loadIndex(m_store->root(), &ok);
    qint64 bytes = 0;
    QDirIterator it(m_store->root(), { "*.jar" }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        bytes += it.fileInfo().size();
    }
    ui->labelStatus->setText(tr("Store: %1\n"
                                "Unique mods: %2   ·   Physical size: %3 MB   ·   Folders consolidated: %4")
                                 .arg(m_store->root(), QString::number(index.entries.size()))
                                 .arg(bytes / (1 << 20))
                                 .arg(index.roots.size()));
}

void ModStoreDialog::runConsolidate(bool dryRun)
{
    QString folder = QFileDialog::getExistingDirectory(this, tr("Choose a folder of instances to %1").arg(dryRun ? tr("dry-run") : tr("consolidate")));
    if (folder.isEmpty())
        return;

    QMessageBox::information(this, tr("How consolidation works"),
                             tr("The first copy of every unique mod jar is moved into the store "
                                "(%1, organized as minecraft_version/mod_name/mod_version.jar) and every "
                                "duplicate is replaced with a hardlink, so identical files across instances "
                                "occupy one physical copy.\n\n"
                                "Instance mods folders keep working exactly as before — this is safe to re-run "
                                "at any time. Use a dry run first if you are unsure.")
                                 .arg(m_store->root()));

    auto task = std::make_unique<ModStoreConsolidateTask>(m_store, folder, dryRun);
    auto* taskPtr = task.get();
    ProgressDialog dlg(this);
    dlg.setWindowTitle(dryRun ? tr("Dry run") : tr("Consolidate mods"));
    int result = dlg.execWithTask(std::move(task));
    if (result == QDialog::Accepted) {
        ui->textReport->appendPlainText("=== " + QDir(folder).dirName() + " ===\n" + taskPtr->summary() + "\n");
    } else {
        ui->textReport->appendPlainText("=== " + QDir(folder).dirName() + " ===\nFAILED: " + taskPtr->failReason() + "\n");
    }
    refreshStats();
}

void ModStoreDialog::on_btnConsolidate_clicked()
{
    runConsolidate(false);
}

void ModStoreDialog::on_btnDryRun_clicked()
{
    runConsolidate(true);
}

void ModStoreDialog::on_btnGC_clicked()
{
    auto ret = QMessageBox::question(this, tr("Garbage collect"),
                                     tr("Delete store entries that are no longer referenced by any instance under "
                                        "the registered folders or this launcher's instances?"));
    if (ret != QMessageBox::Yes)
        return;

    auto task = std::make_unique<ModStoreGcTask>(m_store, QDir(APPLICATION->dataRoot()).filePath("instances"));
    auto* taskPtr = task.get();
    ProgressDialog dlg(this);
    dlg.setWindowTitle(tr("Mod store garbage collection"));
    int result = dlg.execWithTask(std::move(task));
    if (result == QDialog::Accepted)
        ui->textReport->appendPlainText("=== GC ===\n" + taskPtr->summary() + "\n");
    else
        ui->textReport->appendPlainText("=== GC ===\nFAILED: " + taskPtr->failReason() + "\n");
    refreshStats();
}

void ModStoreDialog::on_btnOpenFolder_clicked()
{
    DesktopServices::openPath(QFileInfo(m_store->root()));
}
