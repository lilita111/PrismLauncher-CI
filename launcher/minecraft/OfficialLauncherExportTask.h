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

class MinecraftInstance;

/**
 * Exports an instance as an installation of the official Minecraft Launcher:
 *  - writes a Fabric-loader version profile (versions/<id>/<id>.json) when the
 *    instance uses the Fabric loader, fetched from the Fabric meta API,
 *  - copies mods/, config/ and options.txt into a dedicated game directory
 *    inside the official launcher's .minecraft folder,
 *  - registers (or updates) a profile in launcher_profiles.json, with a backup.
 *
 * Vanilla-only instances are supported too: no version profile is written and
 * the profile simply points lastVersionId at the vanilla version id.
 * Other mod loaders (Forge/NeoForge/Quilt) are rejected for now.
 */
class OfficialLauncherExportTask : public Task {
    Q_OBJECT
   public:
    explicit OfficialLauncherExportTask(MinecraftInstance* instance, QString minecraftDir = QString(), QObject* parent = nullptr);

    static QString defaultMinecraftDir();

    //! Human-readable summary of what was done; valid after success.
    QString summary() const { return m_summary; }

   protected:
    void executeTask() override;

   private:
    QString fetchVersionJson(const QString& url, bool& ok);

    MinecraftInstance* m_instance;
    QString m_minecraftDir;
    QString m_summary;
};
