#pragma once
#include <QKeySequence>
#include <QVariantList>
#include <QStringList>

inline QString readableKey(int key) {
    switch(key) {
    case Qt::Key_Control: return QStringLiteral("Ctrl");
    case Qt::Key_Shift: return QStringLiteral("Shift");
    case Qt::Key_Alt: return QStringLiteral("Alt");
    case Qt::Key_Meta: return QStringLiteral("Win");
    case Qt::Key_CapsLock: return QStringLiteral("CapsLock");
    default: return QKeySequence(key).toString(QKeySequence::PortableText);
    }
}

class InputHistory {
public:
    struct Item { QString key; int count; bool held; };
    QList<Item> items;
    void record(const QString &event) {
        const bool down=event.endsWith(QStringLiteral(" ↓"));
        const bool up=event.endsWith(QStringLiteral(" ↑"));
        if(!down && !up) {
            if(event==QStringLiteral("重置")) { for(auto &item:items) item.held=false; }
            return;
        }
        const QString key=event.left(event.size()-2);
        if(up) {
            for(int i=items.size()-1;i>=0;--i) if(items[i].key==key && items[i].held) {
                items[i].held=false; return;
            }
            return;
        }
        for(const auto &item:items) if(item.key==key && item.held) return;
        if(!items.isEmpty() && items.last().key==key && !items.last().held) {
            ++items.last().count; items.last().held=true; return;
        }
        if(items.size()>=32) items.removeFirst();
        items.append({key,1,true});
    }
    QVariantList entries() const {
        QVariantList result;
        for(const auto &item:items) result.append(QVariantMap{{"key",item.key},{"count",item.count},{"held",item.held}});
        return result;
    }
    QStringList labels() const {
        QStringList result;
        for(const auto &item:items) result.append(item.key+(item.count>1?QStringLiteral(" ×%1").arg(item.count):QString())+(item.held?QStringLiteral(" ↓"):QString()));
        return result;
    }
};
