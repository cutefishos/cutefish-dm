#include "wire.h"

#include <QJsonDocument>
#include <cstring>

namespace dm {

QByteArray encodeFrame(const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    const quint32 length = quint32(payload.size());

    QByteArray frame(reinterpret_cast<const char *>(&length), sizeof(length));
    frame.append(payload);
    return frame;
}

std::optional<QJsonObject> FrameReader::next()
{
    if (m_failed || m_buffer.size() < qsizetype(sizeof(quint32)))
        return std::nullopt;

    quint32 length = 0;
    std::memcpy(&length, m_buffer.constData(), sizeof(length));
    if (length > kMaxFrameSize) {
        m_failed = true;
        return std::nullopt;
    }

    if (m_buffer.size() < qsizetype(sizeof(quint32) + length))
        return std::nullopt;

    QByteArray payload = m_buffer.mid(sizeof(quint32), length);
    // Only the frame taken out: the bytes after it are the next one.
    explicit_bzero(m_buffer.data(), sizeof(quint32) + length);
    m_buffer.remove(0, sizeof(quint32) + length);

    const auto document = QJsonDocument::fromJson(payload);
    wipe(payload);
    if (!document.isObject()) {
        m_failed = true;
        return std::nullopt;
    }

    return document.object();
}

QByteArray encodeLine(const QJsonObject &message)
{
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

std::optional<QJsonObject> decodeLine(const QByteArray &line)
{
    const auto document = QJsonDocument::fromJson(line.trimmed());
    if (!document.isObject())
        return std::nullopt;

    return document.object();
}

void wipe(QByteArray &bytes)
{
    if (!bytes.isEmpty())
        explicit_bzero(bytes.data(), size_t(bytes.size()));
}

void wipe(QString &text)
{
    if (!text.isEmpty())
        explicit_bzero(text.data(), size_t(text.size()) * sizeof(QChar));
    text.clear();
}

} // namespace dm
