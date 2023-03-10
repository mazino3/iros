#include <app/application.h>
#include <app/layout_engine.h>
#include <graphics/rect.h>

namespace App {
LayoutEngine::LayoutEngine(Widget& parent) : m_parent(parent) {
    set_margins(App::Application::the().default_margins());
}

void LayoutEngine::schedule_layout() {
    parent().deferred_invoke_batched(m_layout_scheduled, [this] {
        // NOTE: instead of this check, it would perhaps be better for
        //       LayoutEngine to be an App::Object.
        if (parent().layout_engine() == this) {
            // FIXME: this is a hack to ensure pointless relayouts don't occur.
            if (m_layout_scheduled) {
                layout();
                parent().invalidate();
            }
        }
    });
}

Rect LayoutEngine::parent_rect() const {
    auto rect = parent().positioned_rect();
    if (margins().top + margins().bottom >= rect.height()) {
        return {};
    }
    if (margins().left + margins().right >= rect.width()) {
        return {};
    }
    return rect.with_x(rect.x() + margins().left)
        .with_width(rect.width() - margins().left - margins().right)
        .with_y(rect.y() + margins().top)
        .with_height(rect.height() - margins().top - margins().bottom);
}
}
