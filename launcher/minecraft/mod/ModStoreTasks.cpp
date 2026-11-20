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

#include "ModStoreTasks.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "ModStore.h"

/** Best-effort Minecraft version of an instance folder (for store paths). */
static QString detectMcVersion(const QString& instanceDir)
{
    // Prism / MultiMC
    QFile mmc(QDir(instanceDir).filePath("mmc-pack.json"));
    if (mmc.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(mmc.readAll());
        for (const auto& c : doc.object().value("components").toArray()) {
            auto comp = c.toObject();
            if (comp.value("uid").toString() == "net.minecraft")
                return comp.value("version").toString();
        }
    }
    // CurseForge
    QFile cf(QDir(instanceDir).filePath("minecraftinstance.json"));
    if (cf.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(cf.readAll());
        auto obj = doc.object();
        for (const char* key : { "gameVersion", "minecraftVersion", "version" }) {
            auto v = obj.value(key).toString();
            if (!v.isEmpty())
                return v;
        }
    }
    return "unknown";
}

struct JarRef {
    QString path;
    QString mcVersion;
};

static QList<JarRef> collectJars(const QString& folderRoot)
{
    QList<JarRef> out;
    QDir root(folderRoot);
    for (const auto& inst : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir mods(inst.absoluteFilePath() + "/mods");
        if (!mods.exists())
            continue;
        QString mc = detectMcVersion(inst.absoluteFilePath());
        QDirIterator it(mods.absolutePath(), { "*.jar" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            out.append({ it.next(), mc });
    }
    return out;
}

void ModStoreConsolidateTask::executeTask()
{
    setStatus(tr("Scanning %1 for mod jars...").arg(m_folderRoot));
    auto jars = collectJars(m_folderRoot);
    int total = jars.size();
    if (total == 0) {
        emitFailed(tr("No mod jars found under %1").arg(m_folderRoot));
        return;
    }

    bool ok = false;
    auto index = ModStore::loadIndex(m_store->root(), &ok);
    if (!ok)
        setStatus(tr("Starting a fresh mod store index..."));

    // remember the root for later GC runs
    if (!index.roots.contains(m_folderRoot))
        index.roots << m_folderRoot;

    qint64 before = 0;
    for (const auto& j : jars)
        before += QFileInfo(j.path).size();

    qint64 storedBytes = 0;
    qint64 linkedBytes = 0;
    int unique = 0, linked = 0, failed = 0;
    QSet<QString> seenThisRun;

    for (int i = 0; i < total; ++i) {
        const auto& jar = jars[i];
        setStatus(tr("Processing %1").arg(QFileInfo(jar.path).fileName()));
        setProgress(i, total * 2);

        QString sha = ModStore::sha1Of(jar.path);
        if (sha.isEmpty()) {
            failed++;
            continue;
        }

        bool already = index.entries.contains(sha) || seenThisRun.contains(sha);
        if (!already) {
            unique++;
            seenThisRun.insert(sha);
            storedBytes += QFileInfo(jar.path).size();
            if (!m_dryRun) {
                QString err;
                auto entry = m_store->ensureStored(index, jar.path, jar.mcVersion, &err);
                if (entry.sha1.isEmpty()) {
                    failed++;
                    continue;
                }
                // the original path now needs a link back (the file was moved into the store)
                if (!m_store->linkInto(entry.relPath, jar.path, &err))
                    failed++;
            }
        } else {
            linked++;
            linkedBytes += QFileInfo(jar.path).size();
            if (!m_dryRun) {
                auto entry = index.entries.value(sha);
                QString err;
                if (!m_store->linkInto(entry.relPath, jar.path, &err))
                    failed++;
            }
        }
    }

    if (!m_dryRun && failed == 0)
        m_store->saveIndex(index);

    qint64 saved = linkedBytes;
    QString mode = m_dryRun ? tr("DRY RUN — nothing was modified") : tr("Consolidation complete");
    m_summary = tr("%1\n\n"
                   "Jars scanned: %2\n"
                   "Unique mods stored: %3 (%9 MB)\n"
                   "Duplicate copies linked: %4\n"
                   "Failures: %5\n"
                   "Disk usage before: %6 MB\n"
                   "Duplicate bytes eliminated: %7 MB (~%8 GiB)")
                      .arg(mode)
                      .arg(total)
                      .arg(unique)
                      .arg(linked)
                      .arg(failed)
                      .arg(before / (1 << 20))
                      .arg(saved / (1 << 20))
                      .arg(saved / double(1 << 30), 0, 'f', 2)
                      .arg(storedBytes / (1 << 20));

    if (failed > 0)
        emitFailed(m_summary);
    else
        emitSucceeded();
}

ModStoreConsolidateTask::ModStoreConsolidateTask(ModStore* store, QString folderRoot, bool dryRun, QObject* parent)
    : Task(parent), m_store(store), m_folderRoot(std::move(folderRoot)), m_dryRun(dryRun)
{}

ModStoreGcTask::ModStoreGcTask(ModStore* store, QString instancesDir, QObject* parent)
    : Task(parent), m_store(store), m_instancesDir(std::move(instancesDir))
{}

void ModStoreGcTask::executeTask()
{
    setStatus(tr("Loading mod store index..."));
    bool ok = false;
    auto index = ModStore::loadIndex(m_store->root(), &ok);
    if (!ok || index.entries.isEmpty()) {
        emitFailed(tr("The mod store is empty."));
        return;
    }

    // scan every registered root + the launcher's own instances for live jars
    QStringList roots = index.roots;
    if (!m_instancesDir.isEmpty() && !roots.contains(m_instancesDir))
        roots << m_instancesDir;

    setStatus(tr("Scanning live instances for referenced mods..."));
    QList<JarRef> live;
    for (const auto& rootPath : roots) {
        if (QDir(rootPath).exists())
            live.append(collectJars(rootPath));
    }
    setProgress(1, 2);

    QSet<QString> liveHashes;
    for (const auto& j : live)
        liveHashes.insert(ModStore::sha1Of(j.path));
    setProgress(2, 2);

    QStringList removed;
    qint64 freed = 0;
    int entriesBefore = index.entries.size();
    for (auto it = index.entries.begin(); it != index.entries.end();) {
        if (!liveHashes.contains(it.key())) {
            freed += it.value().size;
            removed << it.value().relPath;
            m_store->removeEntryFile(it.value().relPath);
            it = index.entries.erase(it);
        } else {
            ++it;
        }
    }

    m_store->saveIndex(index);

    m_summary = tr("Garbage collection complete.\n\n"
                   "Store entries before: %1\n"
                   "Store entries after: %2\n"
                   "Unreferenced entries removed: %3\n"
                   "Space reclaimed: %4 MB")
                      .arg(entriesBefore)
                      .arg(index.entries.size())
                      .arg(removed.size())
                      .arg(freed / (1 << 20));
    emitSucceeded();
}
