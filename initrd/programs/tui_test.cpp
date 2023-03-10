#include <app/flex_layout_engine.h>
#include <graphics/color.h>
#include <tinput/terminal_renderer.h>
#include <tui/application.h>
#include <tui/frame.h>

class TestPanel final : public TUI::Frame {
    APP_WIDGET(TUI::Frame, TestPanel)

public:
    TestPanel(Color color, TextAlign alignment, TInput::TerminalRenderer::BoxStyle box_style, String text)
        : m_color(color), m_alignment(alignment), m_box_style(box_style), m_text(move(text)) {}

    virtual void did_attach() {
        set_frame_color(m_color.invert());
        set_box_style(m_box_style);
        set_accepts_focus(true);

        on<App::MouseDownEvent>([this](const App::MouseDownEvent&) {
            set_frame_color(m_color);
            m_color = m_color.invert();
            invalidate();
            return true;
        });

        on<App::KeyDownEvent>([this](const App::KeyDownEvent&) {
            set_frame_color(m_color);
            m_color = m_color.invert();
            invalidate();
            return true;
        });

        TUI::Frame::did_attach();
    }

    virtual Option<Point> cursor_position() override { return { relative_inner_rect().top_left() }; }

    virtual void render() override {
        Frame::render();

        auto renderer = get_renderer_inside_frame();
        renderer.clear_rect(sized_inner_rect(), m_color);

        auto style = TInput::TerminalTextStyle {
            .foreground = {},
            .background = {},
            .bold = true,
            .invert = false,
        };
        renderer.render_text(sized_inner_rect(), m_text.view(), style, m_alignment);
    }

private:
    Color m_color;
    TextAlign m_alignment;
    TInput::TerminalRenderer::BoxStyle m_box_style;
    String m_text;
};

int main() {
    auto app = TUI::Application::try_create();
    if (!app) {
        fprintf(stderr, "tui_test: standard input is not a terminal\n");
        return 1;
    }

    vga_color colors[9] = {
        VGA_COLOR_BLACK, VGA_COLOR_BLUE,    VGA_COLOR_BROWN, VGA_COLOR_CYAN,  VGA_COLOR_DARK_GREY,
        VGA_COLOR_GREEN, VGA_COLOR_MAGENTA, VGA_COLOR_RED,   VGA_COLOR_WHITE,
    };
    TextAlign alignments[9] = {
        TextAlign::TopLeft,     TextAlign::TopCenter,  TextAlign::TopRight,     TextAlign::CenterLeft,  TextAlign::Center,
        TextAlign::CenterRight, TextAlign::BottomLeft, TextAlign::BottomCenter, TextAlign::BottomRight,
    };
    TInput::TerminalRenderer::BoxStyle box_styles[9] = {
        TInput::TerminalRenderer::BoxStyle::Thick,  TInput::TerminalRenderer::BoxStyle::Thin,
        TInput::TerminalRenderer::BoxStyle::Thick,  TInput::TerminalRenderer::BoxStyle::ThinDashed,
        TInput::TerminalRenderer::BoxStyle::Double, TInput::TerminalRenderer::BoxStyle::ThinDashed,
        TInput::TerminalRenderer::BoxStyle::Thick,  TInput::TerminalRenderer::BoxStyle::Ascii,
        TInput::TerminalRenderer::BoxStyle::Thick,
    };

    auto& main_widget = app->root_window().set_main_widget<TUI::Panel>();
    auto& layout = main_widget.set_layout_engine<App::VerticalFlexLayoutEngine>();
    for (int i = 0; i < 3; i++) {
        auto& horizontal_panel = layout.add<TUI::Panel>();
        auto& horizontal_layout = horizontal_panel.set_layout_engine<App::HorizontalFlexLayoutEngine>();
        for (int j = 0; j < 3; j++) {
            auto index = i * 3 + j;
            horizontal_layout.add<TestPanel>(colors[index], alignments[index], box_styles[index], String::format("message %d", index + 1));
        }
    }

    auto key_bindings = App::KeyBindings {};
    key_bindings.add({ App::Key::Y, App::KeyModifier::Control, App::KeyShortcut::IsMulti::Yes }, [] {
        TUI::Application::the().main_event_loop().set_should_exit(true);
    });
    app->root_window().set_key_bindings(move(key_bindings));

    app->set_use_mouse(true);
    app->set_use_alternate_screen_buffer(true);
    app->enter();
    return 0;
}
