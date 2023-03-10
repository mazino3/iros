#pragma once

#include <app/forward.h>
#include <app/widget.h>

namespace App {
struct Margins {
    int top { 0 };
    int right { 0 };
    int bottom { 0 };
    int left { 0 };
};

class LayoutEngine {
public:
    explicit LayoutEngine(Widget& parent);
    virtual ~LayoutEngine() {}

    virtual void layout() = 0;
    virtual void do_add(Widget& child) = 0;
    virtual void do_remove(Widget& child) = 0;

    void maybe_force_layout() {
        // FIXME: also cancel/ignore the scheduled layout as well.
        if (m_layout_scheduled) {
            layout();
            m_layout_scheduled = false;
        }
    }
    void schedule_layout();

    void set_margins(const Margins& m) { m_margins = m; }
    const Margins& margins() const { return m_margins; }

    template<typename WidgetType, typename... Args>
    WidgetType& add(Args&&... args) {
        auto& result = WidgetType::create(&parent(), forward<Args>(args)...);
        do_add(result.base());
        schedule_layout();
        return result;
    }

    template<typename WidgetType, typename... Args>
    SharedPtr<WidgetType> add_owned(Args&&... args) {
        auto result = WidgetType::create_owned(&parent(), forward<Args>(args)...);
        do_add(result->base());
        schedule_layout();
        return result;
    }

    void remove(Widget& child) {
        do_remove(child);
        if (!child.hidden()) {
            schedule_layout();
        }
    }

protected:
    Rect parent_rect() const;

    Widget& parent() { return m_parent; }
    const Widget& parent() const { return m_parent; }

private:
    Widget& m_parent;
    Margins m_margins;
    bool m_layout_scheduled { false };
};
}
