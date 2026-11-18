// Read the crashing thread's frames from Android's debuggerd tombstone.proto.
// Skip registers, memory dumps, paths to user documents and all other threads.
#pragma once
#include <QByteArrayView>
#include <QStringList>

namespace DrnasCrashFrames {
inline bool varint(QByteArrayView& data, quint64& value) {
    value = 0;
    for (unsigned shift = 0; shift < 64 && !data.empty(); shift += 7) {
        const auto byte = static_cast<quint8>(data.front());
        data = data.sliced(1);
        if (shift == 63 && byte > 1) return false;
        value |= quint64(byte & 127) << shift;
        if (!(byte & 128)) return true;
    }
    return false;
}

template<class Visitor>
bool fields(QByteArrayView data, Visitor visit) {
    while (!data.empty()) {
        quint64 tag, value = 0, length = 0;
        if (!varint(data, tag) || (tag >> 3) == 0) return false;
        switch (tag & 7) {
        case 0: if (!varint(data, value)) return false; break;
        case 1: length = 8; break;
        case 2: if (!varint(data, length)) return false; break;
        case 5: length = 4; break;
        default: return false;
        }
        if (length > quint64(data.size())) return false;
        visit(tag, value, data.first(qsizetype(length)));
        data = data.sliced(qsizetype(length));
    }
    return true;
}

inline QStringList decode(QByteArrayView tombstone) {
    quint64 tid = 0;
    QList<QByteArrayView> entries;
    if (!fields(tombstone, [&](quint64 tag, quint64 value, QByteArrayView data) {
        if (tag == (6 << 3)) tid = value;
        if (tag == ((16 << 3) | 2)) entries.append(data);
    }) || !tid) return {};
    for (auto entry : entries) {
        quint64 key = 0;
        QByteArrayView thread;
        if (!fields(entry, [&](quint64 tag, quint64 value, QByteArrayView data) {
            if (tag == 8) key = value;
            if (tag == 18) thread = data;
        }) || key != tid) continue;
        QStringList lines;
        if (!fields(thread, [&](quint64 tag, quint64, QByteArrayView data) {
            if (tag != ((4 << 3) | 2) || lines.size() >= 32) return;
            quint64 pc = 0;
            QString module, function;
            if (fields(data, [&](quint64 field, quint64 value, QByteArrayView bytes) {
                if (field == 8) pc = value;
                if (field == ((6 << 3) | 2))
                    module = QString::fromUtf8(bytes).section('/', -1);
                if (field == ((4 << 3) | 2)) function = QString::fromUtf8(bytes).left(240);
            })) lines.append(QStringLiteral("  #%1 %2 +0x%3 %4")
                .arg(lines.size()).arg(module).arg(pc, 0, 16).arg(function));
        })) return {};
        return lines;
    }
    return {};
}
}
