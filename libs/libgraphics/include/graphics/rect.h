#pragma once

#include <graphics/point.h>
#include <liim/utilities.h>

class Rect {
public:
    constexpr Rect() {}
    constexpr Rect(int x, int y, int width, int height) : m_x(x), m_y(y), m_width(width), m_height(height) {}
    constexpr Rect(const Point& a, const Point& b)
        : m_x(min(a.x(), b.x())), m_y(min(a.y(), b.y())), m_width(abs(a.x() - b.x())), m_height(abs(a.y() - b.y())) {}

    constexpr int x() const { return m_x; }
    constexpr int y() const { return m_y; }
    constexpr int width() const { return m_width; }
    constexpr int height() const { return m_height; }

    constexpr bool empty() const { return m_width == 0 || m_height == 0; }

    constexpr int left() const { return m_x; }
    constexpr int right() const { return m_x + m_width; }
    constexpr int top() const { return m_y; }
    constexpr int bottom() const { return m_y + m_height; }

    constexpr void set_x(int x) { m_x = x; }
    constexpr void set_y(int y) { m_y = y; }
    constexpr void set_width(int width) { m_width = width; }
    constexpr void set_height(int height) { m_height = height; }

    constexpr Point top_left() const { return { left(), top() }; }
    constexpr Point top_right() const { return { right(), top() }; }
    constexpr Point bottom_left() const { return { left(), bottom() }; }
    constexpr Point bottom_right() const { return { right(), bottom() }; }
    constexpr Point center() const { return Point(x() + width() / 2, y() + height() / 2); }

    constexpr Rect top_edge() const { return { x(), y(), width(), 1 }; }
    constexpr Rect right_edge() const { return { x() + width() - 1, y(), 1, height() }; }
    constexpr Rect bottom_edge() const { return { x(), y() + height() - 1, width(), 1 }; }
    constexpr Rect left_edge() const { return { x(), y(), 1, height() }; }

    constexpr bool intersects(Point p) const { return p.x() >= left() && p.x() < right() && p.y() >= top() && p.y() < bottom(); }
    constexpr bool intersects(const Rect& other) const {
        return !this->empty() && !other.empty() &&
               !(this->x() >= other.x() + other.width() || this->x() + this->width() <= other.x() ||
                 this->y() >= other.y() + other.height() || this->y() + this->height() <= other.y());
    }

    constexpr Rect intersection_with(const Rect& other) const {
        if (!intersects(other)) {
            return {};
        }

        auto x = max(this->x(), other.x());
        auto y = max(this->y(), other.y());
        auto end_x = min(this->x() + this->width(), other.x() + other.width());
        auto end_y = min(this->y() + this->height(), other.y() + other.height());
        return { x, y, end_x - x, end_y - y };
    }

    constexpr Rect adjusted(int d) const { return adjusted(d, d); }
    constexpr Rect adjusted(int dx, int dy) const { return { x() - dx, y() - dy, width() + 2 * dx, height() + 2 * dy }; }

    constexpr Rect translated(int d) const { return translated(d, d); }
    constexpr Rect translated(int dx, int dy) const { return { x() + dx, y() + dy, width(), height() }; }
    constexpr Rect translated(const Point& p) const { return translated(p.x(), p.y()); }

    constexpr Rect positioned(int x) const { return positioned(x, x); }
    constexpr Rect positioned(int x, int y) const { return { x, y, width(), height() }; }
    constexpr Rect positioned(Point p) const { return positioned(p.x(), p.y()); }

    constexpr Rect expanded(int d) const { return expanded(d, d); }
    constexpr Rect expanded(int dx, int dy) const { return { x(), y(), width() + dx, height() + dy }; }

    constexpr Rect shrinked(int d) const { return shrinked(d, d); }
    constexpr Rect shrinked(int dx, int dy) const {
        auto rect = Rect { x(), y(), width() - dx, height() - dy };
        if (rect.width() < 0 || rect.height() < 0) {
            return {};
        }
        return rect;
    }

    constexpr Rect with_x(int x) const { return { x, y(), width(), height() }; }
    constexpr Rect with_y(int y) const { return { x(), y, width(), height() }; }
    constexpr Rect with_width(int width) const { return { x(), y(), width, height() }; }
    constexpr Rect with_height(int height) const { return { x(), y(), width(), height }; }

    constexpr bool operator==(const Rect& other) const {
        return this->x() == other.x() && this->y() == other.y() && this->width() == other.width() && this->height() == other.height();
    };
    constexpr bool operator!=(const Rect& other) const { return !(*this == other); }

private:
    int m_x { 0 };
    int m_y { 0 };
    int m_width { 0 };
    int m_height { 0 };
};

namespace LIIM::Format {
template<>
struct Formatter<Rect> : public Formatter<String> {
    void format(const Rect& r, FormatContext& context) {
        return format_to_context(context, "Rect <x={} y={} width={} height={}>", r.x(), r.y(), r.width(), r.height());
    }
};
}
