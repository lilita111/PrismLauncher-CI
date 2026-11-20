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

#pragma once

#include "tasks/Task.h"

class ModStore;

/**
 * One-time (and repeatable) consolidation: walks a folder of instances, moves
 * the first copy of every unique mod jar into the ModStore and replaces every
 * original path with a hardlink, so identical jars across instances occupy one
 * physical copy. Dry-run mode computes the exact same report without touching
 * anything.
 */
class ModStoreConsolidateTask : public Task {
    Q_OBJECT
   public:
    ModStoreConsolidateTask(ModStore* store, QString folderRoot, bool dryRun, QObject* parent = nullptr);

    QString summary() const { return m_summary; }

   protected:
    void executeTask() override;

   private:
    ModStore* m_store;
    QString m_folderRoot;
    bool m_dryRun;
    QString m_summary;
};

/**
 * Garbage collection: deletes store entries whose content is no longer
 * referenced by any live jar under the registered consolidation roots or the
 * launcher's own instances directory.
 */
class ModStoreGcTask : public Task {
    Q_OBJECT
   public:
    ModStoreGcTask(ModStore* store, QString instancesDir, QObject* parent = nullptr);

    QString summary() const { return m_summary; }

   protected:
    void executeTask() override;

   private:
    ModStore* m_store;
    QString m_instancesDir;
    QString m_summary;
};
