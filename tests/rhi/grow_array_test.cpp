#include <catch2/catch_test_macros.hpp>

#include "rhi/grow_array.h"

using engine_placeholder::rhi::detail::GrowArray;

TEST_CASE("GrowArray: pushing or inserting one of its own elements works when the array must grow", "[rhi][grow_array]") {
    GrowArray<int> a;
    a.Push(5);
    // Grow happens at the sizes the array doubles at; push the first element each time until several growths have happened.
    for (int i = 0; i < 40; ++i) {
        a.Push(a[0]);
        CHECK(a.Back() == 5);
    }
    a.Push(9);
    // Insert at the front a copy of the last element (the shift moves it, and growth may free it).
    for (int i = 0; i < 40; ++i) {
        const int last = a.Back();
        a.Insert(0, a.Back());
        CHECK(a[0] == last);
    }
    CHECK(a.Size() == 82);
}
