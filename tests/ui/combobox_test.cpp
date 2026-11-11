#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widgets/combobox.h"
#include "ui/widgets/text_field.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

Rect CRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

class Fruit : public ListModel
{
public:
    usize ItemCount() const override { return 5; }
    StringView ItemText(usize i) override
    {
        static const char* const names[] = {"Apple", "Apricot", "Banana", "Cherry", "Date"};
        return StringView(names[i]);
    }
};

// A model of numbers, to show that the drop-down is virtual.
class Numbers : public ListModel
{
public:
    usize ItemCount() const override { return 100000; }
    StringView ItemText(usize i) override
    {
        StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
        sb.AppendInt(static_cast<i64>(i));
        text_ = String<HeapAllocator>(HeapAllocator::Default(), sb.View());
        return text_.View();
    }

private:
    String<HeapAllocator> text_{HeapAllocator::Default(), StringView()};
};

struct ComboRig
{
    explicit ComboRig(ListModel* model = nullptr) : h(400, 300, 1, StringView(UI_TEST_FONT_FILE))
    {
        UniquePtr<ComboBox> c = MakeUnique<ComboBox>(StringView("combo"));
        combo = c.Get();
        combo->SetModel(model != nullptr ? model : &fruit);
        combo->SetBounds(CRect(20, 20, 160, 28));
        combo->OnSelectionChanged().Connect(&ComboRig::Changed, this);
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(c.Release()));
        h.Advance(0);
        h.GetSurface().SetFocus(combo);
    }
    static void Changed(void* user, ComboBox*) { ++static_cast<ComboRig*>(user)->changes; }
    Harness h;
    Fruit fruit;
    ComboBox* combo = nullptr;
    int changes = 0;
};

}  // namespace

TEST_CASE("Up and Down select the previous and next item when closed", "[ui][combobox]")
{
    ComboRig rig;
    CHECK(rig.combo->Selected() == ComboBox::kNone);
    rig.h.Key(Key::Down);
    CHECK(rig.combo->Selected() == 0);
    rig.h.Key(Key::Down);
    rig.h.Key(Key::Down);
    CHECK(rig.combo->Selected() == 2);
    rig.h.Key(Key::Up);
    CHECK(rig.combo->Selected() == 1);
    CHECK(rig.changes == 4);
    rig.h.Key(Key::End);
    CHECK(rig.combo->Selected() == 4);
    rig.h.Key(Key::Down);  // at the end: no change, no callback
    CHECK(rig.combo->Selected() == 4);
    CHECK(rig.changes == 5);
    rig.h.Key(Key::Home);
    CHECK(rig.combo->Selected() == 0);
    CHECK(rig.combo->Text().View() == StringView("Apple"));
}

TEST_CASE("letters select by type-ahead", "[ui][combobox]")
{
    ComboRig rig;
    rig.h.TypeText(StringView("b"));
    CHECK(rig.combo->Selected() == 2);
    rig.h.TypeText(StringView("a"));
    rig.h.GetSurface().SetTime(rig.h.GetSurface().Time() + 5);
    rig.h.TypeText(StringView("a"));  // a new prefix after the pause
    CHECK(rig.combo->Selected() == 0);
    rig.h.TypeText(StringView("a"));  // the same letter again cycles
    CHECK(rig.combo->Selected() == 1);
    rig.h.GetSurface().SetTime(rig.h.GetSurface().Time() + 5);
    rig.h.TypeText(StringView("a"));
    rig.h.TypeText(StringView("p"));
    rig.h.TypeText(StringView("r"));
    CHECK(rig.combo->Selected() == 1);  // "apr": Apricot
}

TEST_CASE("the wheel selects only when switched on", "[ui][combobox]")
{
    ComboRig rig;
    rig.h.Wheel(50, 30, 0, 1);
    CHECK(rig.combo->Selected() == ComboBox::kNone);
    rig.combo->SetWheelSelects(true);
    rig.h.Wheel(50, 30, 0, 1);
    CHECK(rig.combo->Selected() == 0);
    rig.h.Wheel(50, 30, 0, 1);
    CHECK(rig.combo->Selected() == 1);
    rig.h.Wheel(50, 30, 0, -1);
    CHECK(rig.combo->Selected() == 0);
}

TEST_CASE("Alt+Down, F4, Space and a click open the list", "[ui][combobox]")
{
    ComboRig rig;
    rig.h.Key(Key::Down, true, os::kModAlt);
    CHECK(rig.combo->IsOpen());
    rig.h.Key(Key::Escape);
    CHECK_FALSE(rig.combo->IsOpen());
    rig.h.Key(Key::F4);
    CHECK(rig.combo->IsOpen());
    rig.h.Key(Key::Escape);
    rig.h.Key(Key::Space);
    CHECK(rig.combo->IsOpen());
    rig.h.Key(Key::Escape);
    rig.h.Click(60, 34);
    CHECK(rig.combo->IsOpen());
    rig.h.Click(60, 34);  // a click on the box again closes it
    CHECK_FALSE(rig.combo->IsOpen());
}

TEST_CASE("Enter in the open list chooses the item and returns the focus", "[ui][combobox]")
{
    ComboRig rig;
    rig.h.Key(Key::F4);
    REQUIRE(rig.combo->IsOpen());
    CHECK(rig.h.GetSurface().Focus() == rig.combo->DropDown());
    rig.h.Key(Key::Down);
    rig.h.Key(Key::Down);
    CHECK(rig.combo->Selected() == ComboBox::kNone);  // moving in the list does not choose
    rig.h.Key(Key::Enter);
    CHECK_FALSE(rig.combo->IsOpen());
    CHECK(rig.combo->Selected() == 1);
    CHECK(rig.changes == 1);
    CHECK(rig.h.GetSurface().Focus() == rig.combo);
}

TEST_CASE("Escape closes the list and keeps the selection", "[ui][combobox]")
{
    ComboRig rig;
    rig.combo->Select(3);
    rig.h.Key(Key::F4);
    CHECK(rig.combo->DropDown()->Current() == 3);  // opens on the selection
    rig.h.Key(Key::Up);
    rig.h.Key(Key::Escape);
    CHECK(rig.combo->Selected() == 3);
    CHECK(rig.changes == 0);
    CHECK(rig.h.GetSurface().Focus() == rig.combo);
}

TEST_CASE("a click on a list item chooses it", "[ui][combobox]")
{
    ComboRig rig;
    rig.h.Key(Key::F4);
    ListBox* list = rig.combo->DropDown();
    REQUIRE(list != nullptr);
    const Rect b = list->Bounds();
    CHECK(b.y >= rig.combo->Bounds().Bottom() - 1);
    CHECK(b.w >= rig.combo->Bounds().w - 1);
    rig.h.Click(b.x + 20, b.y + list->RowHeight() * 2.5f);
    CHECK_FALSE(rig.combo->IsOpen());
    CHECK(rig.combo->Selected() == 2);
}

TEST_CASE("a click outside closes the list without choosing", "[ui][combobox]")
{
    ComboRig rig;
    rig.h.Key(Key::F4);
    rig.h.Click(350, 250);
    CHECK_FALSE(rig.combo->IsOpen());
    CHECK(rig.combo->Selected() == ComboBox::kNone);
}

TEST_CASE("the editable box keeps its text and its selection in step", "[ui][combobox]")
{
    ComboRig rig;
    rig.combo->SetEditable(true);
    rig.h.Advance(0);
    rig.h.GetSurface().SetFocus(rig.combo->Field());
    rig.h.TypeText(StringView("Date"));
    CHECK(rig.combo->Selected() == 4);  // text equal to an item selects it
    CHECK(rig.changes == 1);
    rig.h.TypeText(StringView("s"));
    CHECK(rig.combo->Selected() == ComboBox::kNone);
    CHECK(rig.changes == 2);
    rig.combo->Select(2);
    CHECK(rig.combo->Field()->Text() == StringView("Banana"));
    CHECK(rig.changes == 2);  // programmatic: no callback
    // Down in the field opens the list; choosing puts the item's text in the field.
    rig.h.Key(Key::Down);
    REQUIRE(rig.combo->IsOpen());
    rig.h.Key(Key::Down);
    rig.h.Key(Key::Enter);
    CHECK(rig.combo->Selected() == 3);
    CHECK(rig.combo->Field()->Text() == StringView("Cherry"));
    CHECK(rig.combo->Text().View() == StringView("Cherry"));
}

TEST_CASE("a model of 100,000 items opens a short list", "[ui][combobox]")
{
    Numbers numbers;
    ComboRig rig(&numbers);
    rig.combo->Select(54321);
    rig.h.Key(Key::F4);
    ListBox* list = rig.combo->DropDown();
    REQUIRE(list != nullptr);
    CHECK(list->Current() == 54321);
    CHECK(list->Bounds().h < 300);
    usize first = 0;
    usize end = 0;
    list->VisibleRange(&first, &end);
    CHECK(end - first <= ComboBox::kDropRows + 1);
    CHECK(list->RowLayouts() < 50);
    rig.h.Key(Key::End);
    rig.h.Key(Key::Enter);
    CHECK(rig.combo->Selected() == 99999);
}

TEST_CASE("an empty model does not open", "[ui][combobox]")
{
    ComboRig rig;
    rig.combo->SetModel(nullptr);
    rig.h.Key(Key::F4);
    CHECK_FALSE(rig.combo->IsOpen());
}

TEST_CASE("removing the box closes its list", "[ui][combobox]")
{
    ComboRig rig;
    rig.h.Key(Key::F4);
    REQUIRE(rig.h.GetSurface().PopupCount() == 1);
    rig.h.GetSurface().Root()->RemoveChild(rig.combo);
    rig.h.Advance(0);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("The drop-down opens scrolled to the selected item, though it was selected before it was laid out",
          "[ui][combobox]")
{
    Numbers numbers;
    ComboRig rig(&numbers);
    rig.combo->Select(50000);
    rig.h.Key(Key::F4);
    REQUIRE(rig.combo->IsOpen());
    rig.h.Advance(0);
    ListBox* list = rig.combo->DropDown();
    REQUIRE(list != nullptr);
    CHECK(list->Offset() > 0);  // without the fix the list opened at the top and the choice was 50000 rows away
    const f32 row = list->RowHeight();
    CHECK(list->Offset() <= row * 50000);
    CHECK(list->Offset() + row * 20 >= row * 50000);
}
