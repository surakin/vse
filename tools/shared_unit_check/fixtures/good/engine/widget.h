#pragma once

struct Widget
{
    int value = 0;
};

using WidgetPtr = SharedPtr<Widget>;
using WidgetConstPtr = SharedPtr<const Widget>;
