#include "framework.hpp"

#include "../core/log.hpp"
#include "../drivers/framebuffer.hpp"
#include "../terminal.hpp"
#include "../ui/window_manager.hpp"

namespace {

bool flux_session_start(const char*) {
    if (!graphics::available()) {
        log::write(log::Level::Error, "GUI", "framebuffer unavailable for Flux session");
        return false;
    }

    const windowing::Status status = windowing::initialized()
        ? windowing::Status::Ok
        : windowing::initialize(graphics::width(), graphics::height());
    if (status != windowing::Status::Ok) {
        log::write(log::Level::Error, "GUI", windowing::status_message(status));
        return false;
    }

    terminal::println("[TEST] desktop_session: PASS");
    log::write(log::Level::Info, "GUI", "Kurogane Flux desktop session online");

    // Flux owns GOP from this point onward. stdout/stderr and diagnostic
    // messages remain visible on serial, but the boot terminal may no longer
    // draw glyphs or call framebuffer scroll_up() underneath the desktop.
    terminal::set_framebuffer_output(false);

    windowing::invalidate();
    static_cast<void>(windowing::render_if_needed());
    return true;
}

void flux_session_key(char) {}

void flux_session_tick(uint64_t) {
    // Repaint only when a window operation or userspace UI present marks the
    // WindowManager dirty. Idle desktop must not continuously touch GOP.
    static_cast<void>(windowing::render_if_needed());
}

void flux_session_stop() {
    terminal::set_framebuffer_output(true);
    log::write(log::Level::Warn, "GUI", "Flux desktop session stopped; terminal display restored");
}

void flux_session_input(const input::Event& event) {
    const windowing::Status status = windowing::dispatch(event);
    if (status != windowing::Status::Ok &&
        status != windowing::Status::NotFound &&
        status != windowing::Status::IterationStopped) {
        log::write(log::Level::Warn, "GUI", windowing::status_message(status));
    }
    static_cast<void>(windowing::render_if_needed());
}

} // namespace

extern "C" bool kurogane_start_desktop_session() {
    if (applications::running()) {
        return true;
    }

    const applications::Status registration =
        applications::register_application({
            "flux-session",
            "Kurogane Flux userspace desktop session host",
            flux_session_start,
            flux_session_key,
            flux_session_tick,
            flux_session_stop,
            flux_session_input,
        });
    if (registration != applications::Status::Ok &&
        registration != applications::Status::Duplicate) {
        log::write(
            log::Level::Error,
            "GUI",
            applications::status_message(registration));
        return false;
    }

    const applications::Status launch = applications::launch("flux-session");
    if (launch != applications::Status::Ok) {
        log::write(log::Level::Error, "GUI", applications::status_message(launch));
        return false;
    }
    return true;
}
