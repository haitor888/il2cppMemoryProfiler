#ifndef DETAILSWIDGET_H
#define DETAILSWIDGET_H

#include <QWidget>
#include <QListWidget>

#include <vector>
#include <unordered_map>

namespace Ui {
class DetailsWidget;
}

enum class ThingType;
struct TypeDescription;
struct FieldDescription;
struct BytesAndOffset;
struct ThingInMemory;
struct ManagedObject;
struct CrawledMemorySnapshot;
class PrimitiveValueReader;
class DetailsWidget : public QWidget {
    Q_OBJECT
public:
    explicit DetailsWidget(CrawledMemorySnapshot* snapshot, QWidget *parent = nullptr);
    ~DetailsWidget();

    void ShowThing(ThingInMemory* thing, ThingType type);

signals:
    void ThingSelected(std::uint32_t index);

private slots:
    void OnListItemDoubleClicked(QListWidgetItem *item);

private:
    void SizeToContent(QListWidget* widget);
    void DrawLinks(QListWidget* widget, const std::vector<std::uint64_t>& pointers);
    void DrawLinks(QListWidget* widget, const std::vector<ThingInMemory*> things);
    ThingInMemory* GetThingAt(std::uint64_t address);

    void DrawFields(QListWidget* widget, TypeDescription* type, const BytesAndOffset& bo, bool useStatics = false);
    void DrawFields(QListWidget* widget, ManagedObject* mo);
    void DrawValueFor(QListWidget* widget, const FieldDescription* field, const BytesAndOffset& bo);

private:
    Ui::DetailsWidget *ui;
    CrawledMemorySnapshot* snapshot_;
    PrimitiveValueReader* primitiveValueReader_;
    std::unordered_map<std::uint64_t, ThingInMemory*> managedObjCache_;
};

#endif // DETAILSWIDGET_H
