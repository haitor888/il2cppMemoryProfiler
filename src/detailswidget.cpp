#include "detailswidget.h"
#include "ui_detailswidget.h"

#include <QScrollBar>
#include <QDebug>

#include <algorithm>

#include "umpcrawler.h"
#include "umpmodel.h"

DetailsWidget::DetailsWidget(CrawledMemorySnapshot* snapshot, QWidget *parent) :
    QWidget(parent), ui(new Ui::DetailsWidget),
    snapshot_(snapshot), primitiveValueReader_(new PrimitiveValueReader(snapshot)) {
    ui->setupUi(this);
    ShowThing(nullptr, ThingType::NONE);
    connect(ui->fieldsWidget, &QListWidget::itemDoubleClicked, this, &DetailsWidget::OnListItemDoubleClicked);
    connect(ui->refsListWidget, &QListWidget::itemDoubleClicked, this, &DetailsWidget::OnListItemDoubleClicked);
    connect(ui->refbysListWidget, &QListWidget::itemDoubleClicked, this, &DetailsWidget::OnListItemDoubleClicked);
}

DetailsWidget::~DetailsWidget() {
    delete ui;
}

void DetailsWidget::ShowThing(ThingInMemory* thing, ThingType type) {
    if (type == ThingType::MANAGED) {
        auto managedObj = static_cast<ManagedObject*>(thing);
        auto managedType = managedObj->typeDescription_;
        ui->managedType->setText(managedType->name_);
        ui->managedAddr->setText(QString("%1").arg(managedObj->address_, 0, 16));
        ui->managedSize->setText(sizeToString(managedObj->size_));
        ui->valueListWidget->clear();
        if (managedType->name_ == "System.String") {
            ui->valueListWidget->addItem(
                        CrawledMemorySnapshot::ReadString(
                            snapshot_, CrawledMemorySnapshot::FindInHeap(snapshot_, managedObj->address_)));
        } else if (managedType->IsArray()) {
            int elementCount = CrawledMemorySnapshot::ReadArrayLength(snapshot_, managedObj->address_, managedType);
            int rank = managedType->ArrayRank();
            if (rank != 1) {
                ui->valueListWidget->addItem("Can't display multi-dimension arrays yet.");
            } else if (snapshot_->typeDescriptions_[managedType->baseOrElementTypeIndex_].IsValueType()) {
                ui->valueListWidget->addItem("Can't display valueType arrays yet.");
            } else {
                std::vector<std::uint64_t> pointers;
                for (int i = 0; i < elementCount; i++) {
                    pointers.push_back(
                                primitiveValueReader_->ReadPointer(
                                    managedObj->address_ + static_cast<std::uint64_t>(snapshot_->runtimeInformation_.arrayHeaderSize) +
                                    static_cast<std::uint64_t>(static_cast<std::uint32_t>(i) * snapshot_->runtimeInformation_.pointerSize)));
                }
                DrawLinks(ui->valueListWidget, pointers);
            }
        }
        ui->valueListWidget->setVisible(ui->valueListWidget->count() > 0);
        ui->valuesLabel->setVisible(ui->valueListWidget->isVisible());
        SizeToContent(ui->valueListWidget);
        ui->fieldsWidget->clear();
        DrawFields(ui->fieldsWidget, managedObj);
        ui->fieldsWidget->setVisible(ui->fieldsWidget->count() > 0);
        ui->fieldsLabel->setVisible(ui->fieldsWidget->isVisible());
        SizeToContent(ui->fieldsWidget);
        ui->refbysListWidget->clear();
        DrawLinks(ui->refbysListWidget, managedObj->referencedBy_);
        ui->refbysListWidget->setVisible(ui->refbysListWidget->count() > 0);
        ui->refbysLabel->setVisible(ui->refbysListWidget->isVisible());
        SizeToContent(ui->refbysListWidget);
        ui->refsLabel->setVisible(false);
        ui->refsListWidget->setVisible(false);
        ui->stackedWidget->setCurrentIndex(1);
        return;
    } else if (type == ThingType::STATIC) {
        auto staticObj = static_cast<StaticFields*>(thing);
        ui->staticsType->setText(staticObj->typeDescription_->name_);
        ui->staticsSize->setText(sizeToString(staticObj->size_));
        ui->fieldsWidget->clear();
        BytesAndOffset bo;
        bo.bytes_ = staticObj->typeDescription_->statics_;
        bo.offset_ = 0;
        bo.pointerSize_ = snapshot_->runtimeInformation_.pointerSize;
        DrawFields(ui->fieldsWidget, staticObj->typeDescription_, bo, true);
        ui->fieldsWidget->setVisible(ui->fieldsWidget->count() > 0);
        ui->fieldsLabel->setVisible(ui->fieldsWidget->isVisible());
        SizeToContent(ui->fieldsWidget);
        ui->refbysListWidget->clear();
        DrawLinks(ui->refbysListWidget, staticObj->referencedBy_);
        ui->refbysListWidget->setVisible(ui->refbysListWidget->count() > 0);
        ui->refbysLabel->setVisible(ui->refbysListWidget->isVisible());
        SizeToContent(ui->refbysListWidget);
        ui->refsListWidget->clear();
        DrawLinks(ui->refsListWidget, staticObj->references_);
        ui->refsListWidget->setVisible(ui->refsListWidget->count() > 0);
        ui->refsLabel->setVisible(ui->refsListWidget->isVisible());
        SizeToContent(ui->refsListWidget);
        ui->stackedWidget->setCurrentIndex(2);
        ui->valueListWidget->setVisible(false);
        ui->valuesLabel->setVisible(false);
        return;
    }
    ui->fieldsWidget->setVisible(false);
    ui->fieldsLabel->setVisible(false);
    ui->valueListWidget->setVisible(false);
    ui->valuesLabel->setVisible(false);
    ui->refbysListWidget->setVisible(false);
    ui->refbysLabel->setVisible(false);
    ui->refsLabel->setVisible(false);
    ui->refsListWidget->setVisible(false);
    ui->stackedWidget->setCurrentIndex(0);
}

void DetailsWidget::OnListItemDoubleClicked(QListWidgetItem *item) {
    if (!item->data(Qt::UserRole).isValid())
        return;
    auto index = item->data(Qt::UserRole).toUInt();
    if (index != std::numeric_limits<std::uint32_t>::max())
        emit ThingSelected(index);
}

void DetailsWidget::SizeToContent(QListWidget* widget) {
    auto extra = widget->horizontalScrollBar()->isVisible() ? widget->horizontalScrollBar()->height() : 0;
    widget->setMinimumHeight(std::min(widget->sizeHintForRow(0) * widget->model()->rowCount() + extra + 10, 200));
    widget->updateGeometry();
}

void DetailsWidget::DrawLinks(QListWidget* widget, const std::vector<std::uint64_t>& pointers) {
    std::vector<ThingInMemory*> things(pointers.size());
    for (std::size_t i = 0; i < pointers.size(); i++)
        things[i] = GetThingAt(pointers[i]);
    DrawLinks(widget, things);
}

void DetailsWidget::DrawLinks(QListWidget* widget, const std::vector<ThingInMemory*> things) {
    for (auto thing : things) {
        auto caption = thing == nullptr ? "nullptr" : thing->caption_;
        std::uint64_t addr = 0;
        if (thing && thing->type() == ThingType::MANAGED) {
            auto managed = static_cast<ManagedObject*>(thing);
            if (managed != nullptr && managed->typeDescription_->name_ == "System.String")
                caption = CrawledMemorySnapshot::ReadString(snapshot_, CrawledMemorySnapshot::FindInHeap(snapshot_, managed->address_));
            addr = managed->address_;
        }
        auto widgetItem = new QListWidgetItem();
        widgetItem->setData(Qt::DisplayRole, caption);
        widgetItem->setData(Qt::UserRole, thing != nullptr ? thing->index_ : std::numeric_limits<std::uint32_t>::max());
        widget->addItem(widgetItem);
    }
}

ThingInMemory* DetailsWidget::GetThingAt(std::uint64_t address) {
    if (managedObjCache_.find(address) == managedObjCache_.end()) {
        ThingInMemory* thing = nullptr;
        for (auto& managed : snapshot_->managedObjects_) {
            if (managed.address_ == address) {
                thing = &managed;
                break;
            }
        }
        managedObjCache_[address] = thing;
    }
    return managedObjCache_[address];
}

void DetailsWidget::DrawFields(QListWidget* widget, TypeDescription* type, const BytesAndOffset& bo, bool useStatics) {
    std::vector<const FieldDescription*> fields;
    CrawledMemorySnapshot::AllFieldsOf(snapshot_, type, useStatics ? FieldFindOptions::OnlyStatic : FieldFindOptions::OnlyInstance, fields);
    for (std::size_t i = 0; i < fields.size(); i++) {
        auto field = fields[i];
        DrawValueFor(widget, field, bo.Add(field->offset_));
    }
}

void DetailsWidget::DrawFields(QListWidget* widget, ManagedObject* mo) {
    if (mo->typeDescription_->IsArray())
        return;
    DrawFields(widget, mo->typeDescription_, CrawledMemorySnapshot::FindInHeap(snapshot_, mo->address_));
}

void DetailsWidget::DrawValueFor(QListWidget* widget, const FieldDescription* field, const BytesAndOffset& bo) {
    auto type = &snapshot_->typeDescriptions_[field->typeIndex_];
    if (type->name_ == "System.Int32") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<std::int32_t>(bo)));
    }
    else if (type->name_ == "System.Int64") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<std::int64_t>(bo)));
    }
    else if (type->name_ == "System.UInt32") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<std::uint32_t>(bo)));
    }
    else if (type->name_ == "System.UInt64") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<std::uint64_t>(bo)));
    }
    else if (type->name_ == "System.Int16") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<std::int16_t>(bo)));
    }
    else if (type->name_ == "System.UInt16") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<std::uint16_t>(bo)));
    }
    else if (type->name_ == "System.Byte") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<std::uint8_t>(bo)));
    }
    else if (type->name_ == "System.SByte") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<std::int8_t>(bo)));
    }
    else if (type->name_ == "System.Char") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<std::uint16_t>(bo)));
    }
    else if (type->name_ == "System.Boolean") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadBool(bo)));
    }
    else if (type->name_ == "System.Single") {
        widget->addItem(field->name_ + QString(": %1").arg(static_cast<double>(primitiveValueReader_->ReadInteger<float>(bo))));
    }
    else if (type->name_ == "System.Double") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadInteger<double>(bo)));
    }
    else if (type->name_ == "System.IntPtr") {
        widget->addItem(field->name_ + QString(": %1").arg(primitiveValueReader_->ReadPointer(bo), 0, 16));
    } else {
        if (type->IsValueType()) {
            DrawFields(widget, type, bo);
        } else {
            auto thing = GetThingAt(bo.ReadPointer());
            if (thing == nullptr) {
                widget->addItem(field->name_ + ": nullptr");
            } else {
                DrawLinks(widget, { thing });
            }
        }
    }
}
