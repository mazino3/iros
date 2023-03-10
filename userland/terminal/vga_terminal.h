#pragma once

#include <eventloop/event.h>
#include <terminal/pseudo_terminal.h>
#include <terminal/tty.h>

#include "vga_buffer.h"

class VgaTerminal {
public:
    VgaTerminal(VgaBuffer& vga_buffer);

    void render();
    void on_key_event(const App::KeyEvent& event);
    void on_text_event(const App::TextEvent& event);
    void on_mouse_event(const App::MouseEvent& event);

    int master_fd() const { return m_pseudo_terminal.master_fd(); }
    void drain_master_fd();

private:
    Terminal::PsuedoTerminal m_pseudo_terminal;
    Terminal::TTY m_tty;
    VgaBuffer& m_vga_buffer;
};
