#pragma once
#include "widget.h"

struct Holder
{
    const Widget* widget = nullptr;
    Widget& other;
    engine::Widget* qualified;
    Vector<const Widget*> list;
    SharedPtr<Widget> spelled;
    SharedPtr<const ns::Widget> spelled_const;
    WidgetPtr fine;
    // const Widget* in_a_comment;
    void Take(const Widget* parameter);
};

inline void Use(const Widget* local_parameter)
{
    const Widget* local = local_parameter;
    static_cast<void>(local);
}
