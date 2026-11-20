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

#include "ModStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

ModStore::ModStore(QString root) : m_root(std::move(root))
{
    QDir().mkpath(m_root);
}

QString ModStore::indexPath() const
{
    return QDir(m_root).filePath("index.json");
}

ModStore::Index ModStore::loadIndex(const QString& root, bool* ok)
{
    Index idx;
    QFile f(QDir(root).filePath("index.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return idx;
    }
    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) {
        if (ok)
            *ok = false;
        return idx;
    }
    auto obj = doc.object();
    for (const auto& r : obj.value("roots").toArray())
        idx.roots << r.toString();
    auto entries = obj.value("entries").toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        auto e = it.value().toObject();
        Entry ent;
        ent.sha1 = it.key();
        ent.relPath = e.value("path").toString();
        ent.size = static_cast<qint64>(e.value("size").toDouble());
        ent.mcVersion = e.value("mc").toString();
        ent.modName = e.value("name").toString();
        ent.modVersion = e.value("version").toString();
        idx.entries.insert(ent.sha1, ent);
    }
    if (ok)
        *ok = true;
    return idx;
}

bool ModStore::saveIndex(const Index& index) const
{
    QJsonObject entries;
    for (const auto& ent : index.entries) {
        entries.insert(ent.sha1,
                       QJsonObject{ { "path", ent.relPath },
                                    { "size", static_cast<double>(ent.size) },
                                    { "mc", ent.mcVersion },
                                    { "name", ent.modName },
                                    { "version", ent.modVersion } });
    }
    QJsonObject obj{ { "version", 1 }, { "roots", QJsonArray::fromStringList(index.roots) }, { "entries", entries } };
    QFile f(indexPath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(obj).toJson());
    return true;
}

QString ModStore::sha1Of(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

void ModStore::parseJarName(const QString& fileName, QString& name, QString& version)
{
    QString base = QFileInfo(fileName).completeBaseName();  // strip .jar
    auto pos = base.lastIndexOf('-');
    if (pos > 0 && pos < base.length() - 1) {
        name = base.left(pos);
        version = base.mid(pos + 1);
    } else {
        name = base;
        version = "0";
    }
}

QString ModStore::sanitize(QString segment)
{
    segment.replace(QRegularExpression("[^A-Za-z0-9._]+"), "_");
    if (segment.isEmpty() || segment == "." || segment == "..")
        segment = "_";
    return segment;
}

ModStore::Entry ModStore::ensureStored(Index& index, const QString& jarPath, const QString& mcVersion, QString* err)
{
    Entry result;
    QString sha = sha1Of(jarPath);
    if (sha.isEmpty()) {
        if (err)
            *err = "could not read " + jarPath;
        return result;
    }
    if (index.entries.contains(sha)) {
        // already stored (possibly under another MC version / name) — hardlink to it
        return index.entries.value(sha);
    }

    QString modName, modVersion;
    parseJarName(QFileInfo(jarPath).fileName(), modName, modVersion);

    QString mc = sanitize(mcVersion.isEmpty() ? "unknown" : mcVersion);
    QString relPath = mc + "/" + sanitize(modName) + "/" + sanitize(modVersion) + ".jar";

    // avoid clobbering a different mod version that happens to share the path
    auto existing = index.entries.constEnd();
    for (auto it = index.entries.constBegin(); it != index.entries.constEnd(); ++it) {
        if (it.value().relPath == relPath) {
            existing = it;
            break;
        }
    }
    if (existing != index.entries.constEnd() && existing.key() != sha) {
        relPath = mc + "/" + sanitize(modName) + "/" + sanitize(modVersion + "_" + sha.left(8)) + ".jar";
    }

    QString absPath = QDir(m_root).filePath(relPath);
    QDir().mkpath(QFileInfo(absPath).absolutePath());

    // move (same volume) or copy (cross-volume), then hardlink back to the original location
    if (!QFile::rename(jarPath, absPath)) {
        if (!QFile::copy(jarPath, absPath)) {
            if (err)
                *err = "could not move " + jarPath + " into the store";
            return result;
        }
        QFile::remove(jarPath);
    }
    QFile::setPermissions(absPath, QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);

    result.sha1 = sha;
    result.relPath = relPath;
    result.size = QFileInfo(absPath).size();
    result.mcVersion = mc;
    result.modName = sanitize(modName);
    result.modVersion = sanitize(modVersion);
    index.entries.insert(sha, result);
    return result;
}

bool ModStore::createHardLink(const QString& target, const QString& linkPath, QString* err)
{
#ifdef Q_OS_WIN
    if (CreateHardLinkW(reinterpret_cast<const wchar_t*>(linkPath.utf16()), reinterpret_cast<const wchar_t*>(target.utf16()),
                        nullptr))
        return true;
    if (err)
        *err = QString("CreateHardLink failed (error %1)").arg(GetLastError());
    return false;
#else
    if (::link(target.toUtf8().constData(), linkPath.toUtf8().constData()) == 0)
        return true;
    if (err)
        *err = QString("link() failed");
    return false;
#endif
}

bool ModStore::linkInto(const QString& relPath, const QString& destPath, QString* err) const
{
    QString src = QDir(m_root).filePath(relPath);
    if (QFile::exists(destPath))
        QFile::remove(destPath);
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    QString linkErr;
    if (createHardLink(src, destPath, &linkErr))
        return true;
    // fallback: plain copy (cross-volume, non-NTFS, ...)
    if (QFile::copy(src, destPath))
        return true;
    if (err)
        *err = linkErr;
    return false;
}

bool ModStore::removeEntryFile(const QString& relPath) const
{
    QString abs = QDir(m_root).filePath(relPath);
    if (!QFile::remove(abs))
        return false;
    // prune empty parents up to the store root
    QDir dir(QFileInfo(abs).absolutePath());
    QDir root(m_root);
    while (dir.exists() && dir.absolutePath() != root.absolutePath()) {
        if (!QDir().rmdir(dir.absolutePath()))
            break;
        dir.cdUp();
    }
    return true;
}
