#include "Plugins/PluginManifest.h"
#include "Plugins/PluginPackage.h"

#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class PluginManifestTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesCurrentIinaManifest();
    void rejectsUnknownPermission();
    void rejectsInvalidIdentifier();
    void packageRejectsEscapingEntry();
};

void PluginManifestTests::parsesCurrentIinaManifest()
{
    const QByteArray json = R"JSON({
      "name": "Online Media",
      "identifier": "io.iina.ytdl",
      "version": "0.9.10",
      "author": {"name": "IINA"},
      "entry": "dist/index.js",
      "globalEntry": "dist/global.js",
      "permissions": ["show-osd", "file-system", "network-request"],
      "allowedDomains": ["github.com", "*.example.com"],
      "preferencesPage": "pref.html",
      "preferenceDefaults": {"enabled": true}
    })JSON";
    QString error;
    const auto manifest = PluginManifest::parse(json, &error);
    QVERIFY2(manifest.has_value(), qPrintable(error));
    QCOMPARE(manifest->identifier, QStringLiteral("io.iina.ytdl"));
    QCOMPARE(manifest->globalEntry, QStringLiteral("dist/global.js"));
    QVERIFY(manifest->hasPermission(PluginPermission::NetworkRequest));
    QVERIFY(manifest->hasPermission(PluginPermission::FileSystem));
    QCOMPARE(manifest->allowedDomains.size(), 2);
}

void PluginManifestTests::rejectsUnknownPermission()
{
    const QByteArray json = R"JSON({
      "name":"Bad", "identifier":"com.example.bad", "version":"1.0.0",
      "author":{"name":"Tester"}, "entry":"main.js",
      "permissions":["run-everything"]
    })JSON";
    QString error;
    QVERIFY(!PluginManifest::parse(json, &error));
    QVERIFY(error.contains(QStringLiteral("Unknown plugin permission")));
}

void PluginManifestTests::rejectsInvalidIdentifier()
{
    const QByteArray json = R"JSON({
      "name":"Bad", "identifier":"bad", "version":"1.0.0",
      "author":{"name":"Tester"}, "entry":"main.js"
    })JSON";
    QVERIFY(!PluginManifest::parse(json));
}

void PluginManifestTests::packageRejectsEscapingEntry()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root =
        temporary.filePath(QStringLiteral("bad.iinaplugin"));
    QVERIFY(QDir().mkpath(root));
    QFile manifest(QDir(root).filePath(QStringLiteral("Info.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(R"JSON({
      "name":"Bad", "identifier":"com.example.bad", "version":"1.0.0",
      "author":{"name":"Tester"}, "entry":"../outside.js"
    })JSON");
    manifest.close();
    QFile outside(temporary.filePath(QStringLiteral("outside.js")));
    QVERIFY(outside.open(QIODevice::WriteOnly));
    outside.write("1");
    outside.close();
    QString error;
    QVERIFY(!PluginPackage::load(root, &error));
    QVERIFY(error.contains(QStringLiteral("outside the package")));
}

QTEST_GUILESS_MAIN(PluginManifestTests)
#include "PluginManifestTests.moc"
