#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <optional>

namespace dm {

// greetd's framing (greetd_ipc::codec): native-endian u32 length, JSON object.
constexpr quint32 kMaxFrameSize = 64 * 1024;

QByteArray encodeFrame(const QJsonObject &message);

// An oversized frame or one that is not a JSON object fails it for good.
class FrameReader
{
public:
    void append(const QByteArray &bytes) { m_buffer.append(bytes); }
    std::optional<QJsonObject> next();
    bool failed() const { return m_failed; }

private:
    QByteArray m_buffer;
    bool m_failed = false;
};

// The daemon and its helpers exchange one compact JSON object per line.
QByteArray encodeLine(const QJsonObject &message);
std::optional<QJsonObject> decodeLine(const QByteArray &line);

// Overwrites a buffer that held a password before it is released.
void wipe(QByteArray &bytes);
void wipe(QString &text);

} // namespace dm
