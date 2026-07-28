#include "Network/SecureCredentialStore.h"

#include <QSettings>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <wincred.h>
#endif

namespace {
QString settingsKey(const QString &name)
{
    return QStringLiteral("credentials/%1").arg(name);
}

#ifdef Q_OS_WIN
QString credentialTarget(const QString &name)
{
    return QStringLiteral("Supernova/%1").arg(name);
}
#endif
}

QString SecureCredentialStore::read(const QString &name)
{
#ifdef Q_OS_WIN
    const QString target = credentialTarget(name);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(
            reinterpret_cast<LPCWSTR>(target.utf16()),
            CRED_TYPE_GENERIC, 0, &credential)) {
        return {};
    }
    const QByteArray plain(
        reinterpret_cast<const char *>(credential->CredentialBlob),
        static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return QString::fromUtf8(plain);
#else
    const QByteArray stored = QByteArray::fromBase64(
        QSettings().value(settingsKey(name)).toString().toLatin1());
    return QString::fromUtf8(stored);
#endif
}

bool SecureCredentialStore::write(
    const QString &name, const QString &value)
{
#ifdef Q_OS_WIN
    const QString target = credentialTarget(name);
    if (value.isEmpty()) {
        return CredDeleteW(
                   reinterpret_cast<LPCWSTR>(target.utf16()),
                   CRED_TYPE_GENERIC, 0)
            || GetLastError() == ERROR_NOT_FOUND;
    }
    const QByteArray plain = value.toUtf8();
    const QString userName = QStringLiteral("Supernova");
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName =
        const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.CredentialBlobSize =
        static_cast<DWORD>(plain.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(
        const_cast<char *>(plain.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName =
        const_cast<LPWSTR>(
            reinterpret_cast<LPCWSTR>(userName.utf16()));
    return CredWriteW(&credential, 0);
#else
    QSettings settings;
    if (value.isEmpty()) {
        settings.remove(settingsKey(name));
        settings.sync();
        return settings.status() == QSettings::NoError;
    }
    const QByteArray plain = value.toUtf8();
    settings.setValue(
        settingsKey(name),
        QString::fromLatin1(plain.toBase64()));
    settings.sync();
    return settings.status() == QSettings::NoError;
#endif
}
