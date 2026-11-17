#include "credential_env.h"

#include <QFile>
#include <QStringList>
#include <QByteArray>
#include <QtGlobal>

namespace {

QString unquote(QString value) {
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.at(0);
        const QChar last = value.at(value.size() - 1);
        if ((first == QLatin1Char('"') && last == QLatin1Char('"')) ||
            (first == QLatin1Char('\'') && last == QLatin1Char('\'')))
            value = value.mid(1, value.size() - 2);
    }
    return value;
}

bool plainKey(const QString& key) {
    if (key.isEmpty()) return false;
    for (const QChar& c : key)
        if (!c.isLetterOrNumber() && c != QLatin1Char('_')) return false;
    return true;
}

int loadOne(const QString& path) {
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    int loaded = 0;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QString body = line.startsWith(QLatin1String("export "))
                                 ? line.mid(7).trimmed() : line;
        const int eq = body.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = body.left(eq).trimmed();
        const QString value = unquote(body.mid(eq + 1));
        if (!plainKey(key) || value.isEmpty()) continue;
        const QByteArray name = key.toUtf8();
        // 现场显式注入的环境变量优先；私有文件只补齐缺失项。
        if (!qEnvironmentVariableIsEmpty(name.constData())) continue;
        qputenv(name.constData(), value.toUtf8());
        ++loaded;
    }
    return loaded;
}

}  // namespace

int loadPrivateCredentialFiles(const QString& configDir) {
    const QStringList files = {
        configDir + QStringLiteral("/camera-credentials.env"),
        configDir + QStringLiteral("/nvr-credentials.env"),
        QStringLiteral("/opt/railhmi/config/camera-credentials.env"),
        QStringLiteral("/opt/railhmi/config/nvr-credentials.env"),
    };
    int loaded = 0;
    for (const QString& path : files) loaded += loadOne(path);
    return loaded;
}
