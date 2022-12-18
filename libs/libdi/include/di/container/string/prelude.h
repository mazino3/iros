#pragma once

#include <di/container/string/encoding.h>
#include <di/container/string/erased_string.h>
#include <di/container/string/fixed_string.h>
#include <di/container/string/string.h>
#include <di/container/string/string_view.h>
#include <di/container/string/transparent_encoding.h>
#include <di/container/string/utf8_encoding.h>

namespace di {
namespace encoding = container::string::encoding;

using container::FixedString;
using container::String;
using container::StringView;
using container::TransparentString;
using container::TransparentStringView;
using container::string::ErasedString;
}
