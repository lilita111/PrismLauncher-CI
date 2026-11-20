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

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * A content-deduplicating store for mod jars, shared across instances.
 *
 * Files are organized readably, as requested:
 *
 *   <root>/<minecraft_version>/<mod_name>/<mod_version>.jar
 *
 * plus an index.json keyed by sha1. Because different instances may run the
 * same bytes under different Minecraft versions, physical dedup is still by
 * content: the first copy of a given sha1 is stored once, and every other
 * location (other MC versions, re-installs) is an NTFS hardlink to the same
 * file record. On filesystems/volumes where hardlinks are impossible, entries
 * degrade gracefully to plain copies.
 */
class ModStore {
   public:
    struct Entry {
        QString sha1;
        QString relPath;  // mc/name/version.jar relative to store root
        qint64 size = 0;
        QString mcVersion;
        QString modName;
        QString modVersion;
    };

    struct Index {
        QMap<QString, Entry> entries;  // by sha1
        QStringList roots;           // folders consolidation has covered (for GC)
    };

    explicit ModStore(QString root);

    QString root() const { return m_root; }
    QString indexPath() const;

    static Index loadIndex(const QString& root, bool* ok = nullptr);
    bool saveIndex(const Index& index) const;

    //! sha1 hex of a file, or empty string on read failure.
    static QString sha1Of(const QString& filePath);

    //! Split "<name>-<version>.jar" into (name, version). Best effort.
    static void parseJarName(const QString& fileName, QString& name, QString& version);

    //! Path-segment sanitizer.
    static QString sanitize(QString segment);

    /**
     * Ensure the content of jarPath is stored. If the sha1 is already in the
     * index, returns the existing entry (physical dedup). Otherwise moves
     * (same volume) or copies jarPath into the store, marks it read-only and
     * adds an index entry. Does NOT save the index — callers batch-save.
     * Returns empty Entry on failure (err set).
     */
    Entry ensureStored(Index& index, const QString& jarPath, const QString& mcVersion, QString* err);

    //! Create destPath as a hardlink to the store entry; falls back to copy.
    bool linkInto(const QString& relPath, const QString& destPath, QString* err) const;

    //! Recursively delete a store entry's file and its (now empty) parents.
    bool removeEntryFile(const QString& relPath) const;

    //! Low-level hardlink creation; OS error message in err on failure.
    static bool createHardLink(const QString& target, const QString& linkPath, QString* err);

   private:
    QString m_root;
};
