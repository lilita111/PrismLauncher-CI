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

#include "OfficialLauncherExportTask.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUuid>
#include <QUrl>

#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

OfficialLauncherExportTask::OfficialLauncherExportTask(MinecraftInstance* instance, QString minecraftDir, QObject* parent)
    : Task(parent), m_instance(instance), m_minecraftDir(minecraftDir)
{}

QString OfficialLauncherExportTask::defaultMinecraftDir()
{
#ifdef Q_OS_WIN
    QString appdata = QProcessEnvironment::systemEnvironment().value("APPDATA");
    if (!appdata.isEmpty())
        return QDir(appdata).filePath(".minecraft");
#endif
#ifdef Q_OS_MACOS
    return QDir::home().filePath("Library/Application Support/minecraft");
#else
    return QDir::home().filePath(".minecraft");
#endif
}

QString OfficialLauncherExportTask::fetchVersionJson(const QString& url, bool& ok)
{
    QNetworkAccessManager nam;
    QNetworkReply* reply = nam.get(QNetworkRequest(QUrl(url)));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        ok = false;
        QString err = reply->errorString();
        reply->deleteLater();
        return err;
    }
    QByteArray data = reply->readAll();
    reply->deleteLater();
    if (data.isEmpty()) {
        ok = false;
        return "Empty response from " + url;
    }
    ok = true;
    return QString::fromUtf8(data);
}

void OfficialLauncherExportTask::executeTask()
{
    setStatus(tr("Reading instance components..."));
    setProgress(0, 4);

    if (!m_instance) {
        emitFailed(tr("No instance selected."));
        return;
    }

    if (m_minecraftDir.isEmpty())
        m_minecraftDir = defaultMinecraftDir();

    QDir mcDir(m_minecraftDir);
    if (!mcDir.exists()) {
        emitFailed(tr("The official Minecraft Launcher does not seem to be installed (no .minecraft folder at %1).").arg(m_minecraftDir));
        return;
    }

    auto profile = m_instance->getPackProfile();
    QString mcVersion = profile->getComponentVersion("net.minecraft");
    if (mcVersion.isEmpty()) {
        emitFailed(tr("This instance has no Minecraft component."));
        return;
    }
    QString fabricVersion = profile->getComponentVersion("net.fabricmc.fabric-loader");

    QStringList unsupported;
    for (auto uid : { "net.minecraftforge", "net.neoforged", "org.quiltmc.quilt-loader" }) {
        if (!profile->getComponentVersion(uid).isEmpty())
            unsupported << uid;
    }
    if (!unsupported.isEmpty()) {
        emitFailed(tr("Instances using %1 cannot be exported to the official launcher yet.").arg(unsupported.join(", ")));
        return;
    }

    QString versionId = mcVersion;
    if (!fabricVersion.isEmpty()) {
        setStatus(tr("Fetching Fabric loader profile for %1 / %2...").arg(mcVersion, fabricVersion));
        bool ok = false;
        QString url = QString("https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json").arg(mcVersion, fabricVersion);
        QString json = fetchVersionJson(url, ok);
        if (!ok) {
            emitFailed(tr("Failed to fetch the Fabric loader profile from meta.fabricmc.net: %1").arg(json));
            return;
        }
        QJsonParseError parseError;
        auto doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emitFailed(tr("The Fabric meta API returned an invalid profile: %1").arg(parseError.errorString()));
            return;
        }
        versionId = doc.object().value("id").toString(mcVersion);
        if (versionId.isEmpty()) {
            emitFailed(tr("The Fabric meta API returned a profile without an id."));
            return;
        }

        QDir versionDir = mcDir.filePath("versions/" + versionId);
        if (!versionDir.mkpath(".")) {
            emitFailed(tr("Could not create %1").arg(versionDir.path()));
            return;
        }
        QFile versionFile(versionDir.filePath(versionId + ".json"));
        if (!versionFile.open(QIODevice::WriteOnly)) {
            emitFailed(tr("Could not write %1").arg(versionFile.fileName()));
            return;
        }
        versionFile.write(doc.toJson());
        versionFile.close();
    }

    setStatus(tr("Copying game files..."));
    setProgress(1, 4);

    // dedicated game directory: .minecraft/<sanitized instance name>
    QString safeName = m_instance->name();
    safeName.replace(QRegularExpression("[^A-Za-z0-9._-]+"), "-");
    safeName.replace(QRegularExpression("^[-.]+|[-.]+$"), "");
    if (safeName.isEmpty())
        safeName = "PrismExport";
    QDir gameDir = mcDir.filePath(safeName);

    QStringList copied;
    QStringList failed;
    auto copyRecursively = [&](const QString& from, const QString& to) {
        QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString src = it.next();
            QString rel = QDir(from).relativeFilePath(src);
            QString dst = QDir(to).filePath(rel);
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (QFile::copy(src, dst))
                copied << rel;
            else
                failed << rel;
        }
    };

    // the official launcher rewrites launcher_profiles.json while it is running,
    // which silently drops profiles added behind its back — warn the user
    bool officialRunning = false;
#ifdef Q_OS_WIN
    {
        QProcess tasklist;
        tasklist.start("tasklist", { "/FI", "IMAGENAME eq MinecraftLauncher.exe" });
        if (tasklist.waitForFinished(5000)) {
            QString out = QString::fromLocal8Bit(tasklist.readAllStandardOutput());
            officialRunning = out.contains("MinecraftLauncher.exe") && !out.contains("No tasks are running");
        }
    }
#endif

    QString instGameRoot = m_instance->gameRoot();
    QDir modsSrc(instGameRoot + "/mods");
    if (modsSrc.exists())
        copyRecursively(modsSrc.absolutePath(), gameDir.filePath("mods"));
    QDir configSrc(instGameRoot + "/config");
    if (configSrc.exists())
        copyRecursively(configSrc.absolutePath(), gameDir.filePath("config"));
    QFile optionsSrc(instGameRoot + "/options.txt");
    if (optionsSrc.exists())
        QFile::copy(optionsSrc.fileName(), gameDir.filePath("options.txt"));

    setProgress(2, 4);

    if (!failed.isEmpty()) {
        emitFailed(tr("Failed to copy %1 file(s), starting with: %2").arg(failed.size()).arg(failed.first()));
        return;
    }

    setStatus(tr("Registering profile in the official launcher..."));
    setProgress(3, 4);

    // patch launcher_profiles.json (with a backup)
    QString profilesPath = mcDir.filePath("launcher_profiles.json");
    if (!QFile::exists(profilesPath)) {
        emitFailed(tr("Could not find %1 — start the official Minecraft Launcher once, then try again.").arg(profilesPath));
        return;
    }
    QFile::copy(profilesPath, profilesPath + ".bak-prismexport");

    QFile profilesFile(profilesPath);
    if (!profilesFile.open(QIODevice::ReadOnly)) {
        emitFailed(tr("Could not read %1").arg(profilesPath));
        return;
    }
    QJsonParseError parseError;
    auto profilesDoc = QJsonDocument::fromJson(profilesFile.readAll(), &parseError);
    profilesFile.close();
    if (parseError.error != QJsonParseError::NoError || !profilesDoc.isObject()) {
        emitFailed(tr("%1 is not valid JSON (%2)").arg(profilesPath, parseError.errorString()));
        return;
    }
    auto root = profilesDoc.object();
    auto profiles = root.value("profiles").toObject();

    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // reuse an existing profile with the same name so repeat exports update in place
    QString profileKey;
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        if (it.value().toObject().value("name").toString() == m_instance->name()) {
            profileKey = it.key();
            break;
        }
    }
    if (profileKey.isEmpty())
        profileKey = QUuid::createUuid().toString(QUuid::WithoutBraces);

    auto existing = profiles.value(profileKey).toObject();
    existing.insert("name", m_instance->name());
    existing.insert("type", "custom");
    if (!existing.contains("created"))
        existing.insert("created", now);
    existing.insert("lastUsed", now);
    existing.insert("lastVersionId", versionId);
    existing.insert("gameDir", QDir::toNativeSeparators(gameDir.absolutePath()));
    profiles.insert(profileKey, existing);
    root.insert("profiles", profiles);

    if (!profilesFile.open(QIODevice::WriteOnly)) {
        emitFailed(tr("Could not write %1").arg(profilesPath));
        return;
    }
    profilesFile.write(QJsonDocument(root).toJson());
    profilesFile.close();

    setProgress(4, 4);

    m_summary = tr("Exported '%1' to the official Minecraft Launcher.\n\n"
                   "Version: %2\n"
                   "Game directory: %3\n"
                   "Files copied: %4\n"
                   "Profile: %5\n\n"
                   "Restart the official Minecraft Launcher if it is running; the installation will "
                   "appear in the Installations tab.")
                      .arg(m_instance->name(), versionId, QDir::toNativeSeparators(gameDir.absolutePath()))
                      .arg(copied.size())
                      .arg(m_instance->name());

    if (officialRunning) {
        m_summary += "\n\n" + tr("WARNING: the official Minecraft Launcher is currently running. It rewrites its "
                                 "profile file while open and may drop this installation. Close it and re-run this "
                                 "export to be safe.");
    }

    emitSucceeded();
}
