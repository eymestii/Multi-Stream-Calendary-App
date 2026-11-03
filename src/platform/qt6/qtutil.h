// Conversión entre los std::string UTF-8 del core y QString. Única frontera Qt <-> core.
#pragma once
#include <QString>
#include <string>

inline QString qs(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
inline std::string ss(const QString& s) {
    const QByteArray b = s.toUtf8();
    return std::string(b.constData(), static_cast<std::size_t>(b.size()));
}
