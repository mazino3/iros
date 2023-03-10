#include <tinput/terminal_renderer.h>
#include <tui/frame.h>

namespace TUI {
Frame::Frame() {}

Frame::~Frame() {}

void Frame::render() {
    Panel::render();

    auto renderer = get_renderer();
    renderer.draw_rect(sized_rect(), m_frame_color, m_box_style);
}

Rect Frame::sized_inner_rect() const {
    return sized_rect().shrinked(2);
}

Rect Frame::relative_inner_rect() const {
    return sized_rect().adjusted(-1);
}

Rect Frame::positioned_inner_rect() const {
    return positioned_rect().adjusted(-1);
}

void Frame::set_frame_color(Option<Color> c) {
    m_frame_color = c;
    invalidate();
}

void Frame::set_box_style(TInput::TerminalRenderer::BoxStyle box_style) {
    m_box_style = box_style;
    invalidate();
}

TInput::TerminalRenderer Frame::get_renderer_inside_frame() {
    auto renderer = get_renderer();
    renderer.set_clip_rect(relative_inner_rect());
    renderer.set_origin(positioned_rect().top_left().translated({ 1, 1 }));
    return renderer;
}
}
