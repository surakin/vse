#include "ui/widgets/modal_dim.h"

#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui {

ModalDim::ModalDim() : Widget(StringView("modaldim")) {
    LayoutProps props;
    props.fill_parent = true;
    SetProps(props);
}

void ModalDim::Draw(Canvas& canvas) {
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    DrawStyleBox(canvas, Bounds(), surface->Themes().Resolve(widget_kind::kModalDim, VariantId(0), 0, scale), scale);
}

} // namespace engine_placeholder::ui
