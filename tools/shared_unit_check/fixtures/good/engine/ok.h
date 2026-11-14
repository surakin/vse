#pragma once
#include "widget.h"

// A member holds the shared unit through its alias; parameters and locals may point at it for one call.
struct Holder
{
    WidgetPtr widget;
    WidgetConstPtr read_only;
    Vector<WidgetPtr> list;
    int count = 0;
    void Take(const Widget* parameter);
    const Widget& Get() const { return *widget; }
};

inline void Use(const Widget* parameter)
{
    const Widget* local = parameter;
    Widget& again = const_cast<Widget&>(*local);
    static_cast<void>(again);
}

enum class Mode
{
    A,
    B
};
