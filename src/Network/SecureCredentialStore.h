#pragma once

#include <QString>

class SecureCredentialStore final {
public:
    [[nodiscard]] static QString read(const QString &name);
    static bool write(const QString &name, const QString &value);
};
