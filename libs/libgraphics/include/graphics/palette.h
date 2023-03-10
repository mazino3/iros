#pragma once

#include <ext/mapped_file.h>
#include <graphics/color.h>
#include <liim/string.h>

#define ENUMERATE_COLOR_TYPES                                                                     \
    __ENUMERATE_COLOR_TYPE(Background, background, ColorValue::Black)                             \
    __ENUMERATE_COLOR_TYPE(Text, text, ColorValue::White)                                         \
    __ENUMERATE_COLOR_TYPE(Outline, outline, ColorValue::White)                                   \
    __ENUMERATE_COLOR_TYPE(Hover, hover, ColorValue::LightGray)                                   \
    __ENUMERATE_COLOR_TYPE(Selected, selected, ColorValue::DarkGray)                              \
    __ENUMERATE_COLOR_TYPE(TaskbarBackground, taskbarBackground, ColorValue::Black)               \
    __ENUMERATE_COLOR_TYPE(DesktopBackground, desktopBackground, ColorValue::DarkGray)            \
    __ENUMERATE_COLOR_TYPE(WindowTitlebarBackground, windowTitlebarBackground, ColorValue::Black) \
    __ENUMERATE_COLOR_TYPE(WindowOutline, windowOutline, ColorValue::White)

class Palette {
public:
    enum ColorType {
#define __ENUMERATE_COLOR_TYPE(t, l, d) t,
        ENUMERATE_COLOR_TYPES
#undef __ENUMERATE_COLOR_TYPE
            Count,
    };

    static SharedPtr<Palette> create_default();
    static SharedPtr<Palette> create_from_json(const String& path);
    static SharedPtr<Palette> create_from_shared_memory(const String& path, int prot);

    static constexpr size_t byte_size() { return sizeof(uint32_t) * ColorType::Count; }

    Color color(ColorType type) const { return m_colors[type]; }

    const String& name() const { return m_name; }

    void copy_from(const Palette& other);

    Palette(Vector<uint32_t> data, String name) : m_colors(data.vector()), m_color_data(move(data)), m_name(name) {}
    Palette(UniquePtr<ByteBuffer> byte_buffer) : m_colors((uint32_t*) byte_buffer->data()), m_raw_data(move(byte_buffer)) {}

private:
    uint32_t* m_colors { nullptr };
    Vector<uint32_t> m_color_data;
    UniquePtr<ByteBuffer> m_raw_data;
    String m_name;
};
