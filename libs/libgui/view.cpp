#include <gui/view.h>

namespace GUI {
View::View() : ScrollComponent(static_cast<Widget&>(*this)) {}

void View::did_attach() {
    ScrollComponent::attach_to_base(base());
}

void View::invalidate_all() {
    invalidate();
}
}
