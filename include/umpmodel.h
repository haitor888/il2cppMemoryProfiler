#ifndef UMPMODEL_H
#define UMPMODEL_H

#include "umpcrawler.h"


#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

#include <QString>
#include <QVector>

struct UMPSnapshotType {
    QString name_;
    TypeDescription* type_;
    std::vector<ThingInMemory*> objects_;
    std::int64_t size_ = 0;
};

QString sizeToString(qint64 size);

class UMPThingInMemoryModel : public QAbstractTableModel {
public:
    UMPThingInMemoryModel(QObject* parent);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void reset(const UMPSnapshotType& snapshotType, bool isDiff);
    ThingInMemory* thingAt(int row) const { return objects_[row]; }
    int indexOf(const ThingInMemory* thing) const;
private:
    QVector<ThingInMemory*> objects_;
    bool isDiff_ = false;
};

class UMPTypeGroupModel : public QAbstractTableModel {
public:
    UMPTypeGroupModel(CrawledMemorySnapshot* snapshot, QObject* parent);
    ~UMPTypeGroupModel() override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    const UMPSnapshotType& getSubModel(int row) const {
        return types_[row];
    }
    const CrawledMemorySnapshot* getSnapshot() const {
        return snapshot_;
    }
    std::int64_t getTotalSize() const {
        return totalSize_;
    }
private:
    QVector<UMPSnapshotType> types_;
    CrawledMemorySnapshot* snapshot_;
    std::int64_t totalSize_;
};

class UMPTableProxyModel : public QSortFilterProxyModel {
public:
    UMPTableProxyModel(QAbstractItemModel* srcModel, QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) { setSourceModel(srcModel); }
protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
        auto leftData = sourceModel()->data(left, Qt::UserRole);
        auto rightData = sourceModel()->data(right, Qt::UserRole);
        if (leftData.type() == QVariant::String)
            return leftData.toString() < rightData.toString();
        else if (leftData.type() == QVariant::LongLong)
            return leftData.toLongLong() < rightData.toLongLong();
        else
            return leftData.toInt() < rightData.toInt();
    }
};

#endif // UMPMODEL_H
