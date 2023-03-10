#pragma once

#include <liim/byte_buffer.h>
#include <liim/option.h>
#include <liim/utilities.h>

namespace LIIM {
class ByteReader {
public:
    ByteReader() {}
    ByteReader(Span<const uint8_t> data) : m_data(data) {}

    const uint8_t* data() const { return m_data.data(); }

    size_t bytes_total() const { return m_data.size(); }
    size_t byte_offset() const { return m_byte_offset; }
    size_t bytes_remaining() const { return m_data.size() - byte_offset(); }
    bool finished() const { return bytes_remaining() == 0; }

    Span<const uint8_t> span_remaining() const { return m_data.subspan(byte_offset()); }

    void reset() { m_byte_offset = 0; }

    void advance(size_t offset) {
        m_byte_offset += offset;
        assert(byte_offset() <= bytes_total());
    }

    void set_byte_offset(size_t byte_offset) {
        assert(byte_offset <= m_data.size());
        m_byte_offset = byte_offset;
    }

    void set_data(Span<const uint8_t> data) {
        m_byte_offset = 0;
        m_data = move(data);
    }

    Option<uint8_t> next_byte() {
        if (m_bit_offset) {
            m_bit_offset = 0;
            m_bit_buffer = 0;
        }

        return next_byte_impl();
    }

    Option<bool> next_bit() {
        if (m_bit_offset == 0) {
            auto byte = next_byte_impl();
            if (!byte) {
                return {};
            }
            m_bit_buffer = *byte;
        }

        auto bit = !!(m_bit_buffer & (1U << m_bit_offset));
        if (++m_bit_offset == 8) {
            m_bit_offset = 0;
        }
        return bit;
    }

    template<typename T>
    const T* pointer_at_offset(size_t offset) const {
        if (offset + sizeof(T) > m_data.size()) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(&data()[offset]);
    }

    template<typename T>
    const T* sized_pointer_at_offset(size_t offset, size_t explicit_size) const {
        if (offset + explicit_size > m_data.size()) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(&data()[offset]);
    }

private:
    Option<uint8_t> next_byte_impl() {
        if (m_byte_offset >= m_data.size()) {
            return {};
        }
        return data()[m_byte_offset++];
    }

    Span<const uint8_t> m_data;
    size_t m_byte_offset { 0 };
    size_t m_bit_offset { 0 };
    uint8_t m_bit_buffer { 0 };
};

class ByteWriter {
public:
    ByteWriter() {}

    bool full() const { return space_available() == 0; }
    size_t capacity() const { return m_output.size(); }
    size_t bytes_written() const { return m_byte_offset; }
    size_t space_available() const { return capacity() - bytes_written(); }
    uint8_t* data() const { return m_output.data(); }

    Span<uint8_t> span_available() const { return m_output.subspan(bytes_written()); }

    void set_output(Span<uint8_t> output) {
        m_output = output;
        m_byte_offset = 0;
    }

    void extend_output(Span<uint8_t> output) {
        assert(m_output.size() <= output.size());
        m_output = output;
    }

    void advance(size_t count) { set_offset(m_byte_offset + count); }

    void set_offset(size_t offset) {
        assert(m_byte_offset <= capacity());
        m_byte_offset = offset;
    }

    void reset() {
        m_byte_offset = 0;
        m_bit_offset = 0;
    }

    bool write_bit(bool bit) {
        if (m_bit_offset == 8) {
            if (!commit_bits()) {
                return false;
            }
        }

        if (bit) {
            m_bit_buffer |= (1U << m_bit_offset);
        } else {
            m_bit_buffer &= ~(1U << m_bit_offset);
        }

        m_bit_offset++;
        return true;
    }

    bool write_byte(uint8_t byte) {
        if (m_bit_offset > 0) {
            if (!commit_bits()) {
                return false;
            }
        }

        return write_byte_impl(byte);
    }

private:
    bool commit_bits() {
        assert(m_bit_offset > 0);
        if (!write_byte_impl(m_bit_buffer)) {
            return false;
        }
        m_bit_offset = 0;
        m_bit_buffer = 0;
        return true;
    }

    bool write_byte_impl(uint8_t byte) {
        if (full()) {
            return false;
        }

        data()[m_byte_offset++] = byte;
        return true;
    }

    Span<uint8_t> m_output;
    size_t m_byte_offset { 0 };
    size_t m_bit_offset { 0 };
    uint8_t m_bit_buffer { 0 };
};

template<typename T>
static inline Span<const uint8_t> as_readonly_bytes(const T& value) {
    return { reinterpret_cast<const uint8_t*>(&value), sizeof(value) };
}

template<typename T>
static inline Span<const uint8_t> array_as_readonly_bytes(const T* array, size_t items) {
    return { reinterpret_cast<const uint8_t*>(array), sizeof(T) * items };
}

template<typename T>
static inline Span<uint8_t> as_writable_bytes(T& value) {
    return { reinterpret_cast<uint8_t*>(&value), sizeof(value) };
}

template<typename T>
static inline Span<uint8_t> array_as_writable_bytes(T* array, size_t items) {
    return { reinterpret_cast<uint8_t*>(array), sizeof(T) * items };
}
}

using LIIM::array_as_readonly_bytes;
using LIIM::array_as_writable_bytes;
using LIIM::as_readonly_bytes;
using LIIM::as_writable_bytes;
using LIIM::ByteReader;
using LIIM::ByteWriter;
