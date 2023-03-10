#pragma once

#include <gui/window.h>

#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_video.h>

namespace GUI {
class SDLWindow final : public PlatformWindow {
public:
    SDLWindow(Window& window, int x, int y, int width, int height, String name, bool has_alpha, WindowServer::WindowType type,
              wid_t parent_id);
    virtual ~SDLWindow() override;

    virtual SharedPtr<Bitmap> pixels() override;
    virtual void flush_pixels() override;
    virtual void did_resize() override;
    virtual void do_set_visibility(int x, int y, bool visible) override;

    SDL_Window* sdl_window() { return m_sdl_window; }
    SDL_Window* sdl_parent();

private:
    void offset_position(int& x, int& y);

    Window& m_window;
    SDL_Window* m_sdl_window { nullptr };
    SDL_Surface* m_sdl_surface { nullptr };
    SharedPtr<Bitmap> m_bitmap;
};
}
