#pragma once

#include <ipc/gen.h>
#include <liim/pointers.h>
#include <liim/string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/hal/input.h>

typedef int wid_t;

namespace WindowServer {
enum WindowType {
    Application,
    Frameless,
    Taskbar,
};
}

namespace LIIM {
template<>
struct Traits<WindowServer::WindowType> {
    static constexpr bool is_simple() { return true; }
};
}

namespace WindowServer {

// clang-format off
IPC_MESSAGES(Client, 
    (CreateWindowRequest,
        (int, x),
        (int, y),
        (int, width),
        (int, height),
        (String, name),
        (WindowType, type),
        (wid_t, parent_id),
        (bool, has_alpha),
    ),
    (RemoveWindowRequest,
        (wid_t, wid),
    ),
    (ChangeWindowVisibilityRequest,
        (wid_t, wid),
        (int, x),
        (int, y),
        (bool, visible),
    ),
    (SwapBufferRequest,
        (wid_t, wid),
    ),
    (WindowReadyToResizeMessage,
        (wid_t, wid),
    ),
    (WindowRenameRequest,
        (wid_t, wid),
        (String, name),
    ),
    (ChangeThemeRequest,
        (String, path),
    ),
    (RegisterAsWindowServerListener),
    (UnregisterAsWindowServerListener),
    (SetActiveWindow,
        (wid_t, wid),
    ),
)

IPC_MESSAGES(Server,
    (CreateWindowResponse,
        (wid_t, wid),
        (size_t, size),
        (String, path),
        (int, width),
        (int, height),
    ),
    (RemoveWindowResponse,
        (bool, success),
    ),
    (ChangeWindowVisibilityResponse,
        (wid_t, wid),
        (bool, success),
    ),
    (KeyEventMessage,
        (wid_t, wid),
        (int, key),
        (int, modifiers),
        (bool, key_down),
        (bool, generates_text),
    ),
    (TextEventMessage,
        (wid_t, wid),
        (String, text),
    ),
    (MouseEventMessage,
        (wid_t, wid),
        (int, x),
        (int, y),
        (int, z),
        (int, buttons),
        (int, modifiers),
    ),
    (WindowDidResizeMessage,
        (wid_t, wid),
    ),
    (WindowReadyToResizeResponse,
        (wid_t, wid),
        (int, new_width),
        (int, new_height),
    ),
    (WindowClosedEventMessage,
        (wid_t, wid),
    ),
    (WindowStateChangeMessage,
        (wid_t, wid),
        (bool, active),
    ),
    (ChangeThemeResponse,
        (bool, success),
    ),
    (ThemeChangeMessage),
    (ServerDidCreatedWindow,
        (wid_t, wid),
        (String, title),
        (WindowType, type),
    ),
    (ServerDidChangeWindowTitle,
        (wid_t, wid),
        (String, new_title),
    ),
    (ServerDidCloseWindow,
        (wid_t, wid),
    ),
    (ServerDidMakeWindowActive,
        (wid_t, wid),
    ),
)
// clang-format on
}
