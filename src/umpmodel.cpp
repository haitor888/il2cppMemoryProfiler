#include "umpmodel.h"
#include "globalLog.h"
#include <QColor>
#include <QDebug>
#include <algorithm>

QString sizeToString(qint64 size) {
    qint64 absSize = std::abs(size);
    if (absSize >= 1024 * 1024 * 1024) {
        return QString::number(static_cast<double>(size) / 1024 / 1024 / 1024, 'f', 2) + " GB";
    } else if (absSize >= 1024 * 1024) {
        return QString::number(static_cast<double>(size) / 1024 / 1024, 'f', 2) + " MB";
    } else if (absSize > 1024) {
        return QString::number(static_cast<double>(size) / 1024, 'f', 2) + " KB";
    } else {
        return QString::number(size) + " Bytes";
    }
}

// UMPManagedObjectModel

UMPThingInMemoryModel::UMPThingInMemoryModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int UMPThingInMemoryModel::rowCount(const QModelIndex &) const {
    return static_cast<int>(objects_.size());
}

int UMPThingInMemoryModel::columnCount(const QModelIndex &) const {
    return 4;
}

QVariant UMPThingInMemoryModel::data(const QModelIndex &index, int role) const {
    int row = index.row();
    int column = index.column();
    if (row >= 0 && row < objects_.size()) {
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            auto mo = objects_[row];
            switch(column) {
                case 0: return mo->caption_;
                case 1: return static_cast<quint32>(mo->referencedBy_.size());
                case 2: return sizeToString(mo->size_);
                case 3:
                    switch(mo->diff_) {
                        case CrawledDiffFlags::kAdded:
                            return "Added";
                        case CrawledDiffFlags::kSame:
                            return "Same";
                        case CrawledDiffFlags::kSmaller:
                            return "Smaller";
                        case CrawledDiffFlags::kBigger:
                            return "Bigger";
                        default:
                            return " ";
                    }
            }
        } else if (role == Qt::UserRole) {
            auto mo = objects_[row];
            switch(column) {
                case 0: return mo->caption_;
                case 1: return static_cast<quint32>(mo->referencedBy_.size());
                case 2: return mo->size_;
                case 3: return static_cast<std::uint8_t>(mo->diff_);
            }
        } else if (role == Qt::BackgroundColorRole) {
            auto mo = objects_[row];
            switch(mo->diff_) {
                case CrawledDiffFlags::kAdded:
                    return QVariant(QColor(Qt::magenta));
                case CrawledDiffFlags::kSmaller:
                    return QVariant(QColor(Qt::green));
                case CrawledDiffFlags::kBigger:
                    return QVariant(QColor(Qt::red));
                default:
                    break;
            }
        }
    }
    return QVariant();
}

QVariant UMPThingInMemoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            switch (section) {
                case 0: return QString("Name");
                case 1: return QString("Refs");
                case 2: return QString("Size");
                case 3: return QString("Flag");
            }
        }
    }
    return QVariant();
}

void UMPThingInMemoryModel::reset(const UMPSnapshotType& snapshotType, bool isDiff) {
    isDiff_ = isDiff;
    beginResetModel();
    objects_.clear();
    auto size = static_cast<int>(snapshotType.objects_.size());
    objects_.reserve(size);
    std::copy(snapshotType.objects_.begin(), snapshotType.objects_.end(), std::back_inserter(objects_));
    endResetModel();
}

int UMPThingInMemoryModel::indexOf(const ThingInMemory* thing) const {
    for (int i = 0; i < objects_.size(); i++) {
        auto object = objects_[i];
        if (object == thing)
            return i;
    }
    return -1;
}

// UMPTypeGroupModel

UMPTypeGroupModel::UMPTypeGroupModel(CrawledMemorySnapshot* snapshot, QObject* parent)
    : QAbstractTableModel(parent), snapshot_(snapshot) {
    std::unordered_map<std::uint32_t, std::vector<ThingInMemory*>> filters;
    for (auto& obj : snapshot->staticFields_) {
        auto& vector = filters[obj.typeDescription_->typeIndex_];
        vector.push_back(&obj);
    }
    for (auto& obj : snapshot->managedObjects_) {
        auto& vector = filters[obj.typeDescription_->typeIndex_];
        vector.push_back(&obj);
    }
    std::unordered_map<std::int64_t, std::int64_t> typeSizes;
    for (const auto& pair : filters)
        typeSizes[pair.first] = 0;
    for (const auto& pair : filters) {
        auto& size = typeSizes[pair.first];
        for (const auto& obj : pair.second)
            size += obj->size_;
    }
    totalSize_ = 0;
    for (std::size_t i = 0; i < snapshot->typeDescriptions_.size(); i++) {
        auto& type = snapshot->typeDescriptions_[i];
        types_.push_back(UMPSnapshotType());
        auto group = &types_.back();
        group->type_ = &type;
        group->name_ = type.name_;
        group->size_ = typeSizes[group->type_->typeIndex_];
        group->objects_ = std::move(filters[group->type_->typeIndex_]);
        totalSize_ += group->size_;



    }
}

UMPTypeGroupModel::~UMPTypeGroupModel() {
    if (snapshot_ != nullptr) {
        CrawledMemorySnapshot::Free(snapshot_);
        delete snapshot_;
    }
}

int UMPTypeGroupModel::rowCount(const QModelIndex &) const {
    return types_.size();
}

int UMPTypeGroupModel::columnCount(const QModelIndex &) const {
    return 4;
}
//row:show memory data
QVariant UMPTypeGroupModel::data(const QModelIndex &index, int role) const {
    int row = index.row();
    int column = index.column();
    if (row >= 0 && row < types_.size()) {
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            auto type = types_[row];
            switch(column) {
                case 1: {
                    //qDebug("readString = %s",qPrintable(type.name_));
                    //GlobalLogDef::writeToFile(row,1) ;
                   // GlobalLogDef::writeToFile(type.name_,0) ;

                    return type.name_;
                }
                case 2: return static_cast<quint32>(type.objects_.size());
                case 3: return sizeToString(type.size_);
                case 0: {
                    //QString str = QString::number(row);
                    //GlobalLogDef::writeToFile(str,1) ;
                   // qDebug("readString = %s",row);
                     return row;

                }
            }

        } else if (role == Qt::UserRole) {
            auto type = types_[row];
            switch(column) {
            case 1: {
                    //qDebug("readString = %s",qPrintable(type.name_));
                    //GlobalLogDef::writeToFile(type.name_,0) ;
                    return type.name_;
                }
                case 2: return static_cast<quint32>(type.objects_.size());
                case 3: return type.size_;
                case 0: return row;
            }




        }



    }

    return QVariant();
}

QVariant UMPTypeGroupModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            switch (section) {
                case 0: return QString("No.");
                case 1: return QString("Type");
                case 2: return QString("Count");
                case 3: return QString("Size");

            }
        }
    }
    return QVariant();
}
