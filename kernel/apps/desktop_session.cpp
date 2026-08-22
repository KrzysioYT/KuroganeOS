#include "framework.hpp"

#include "../core/log.hpp"
#include "../drivers/framebuffer.hpp"
#include "../terminal.hpp"
#include "../ui/window_manager.hpp"

namespace {

bool flux_session_start(const char*) {
    if (!graphics::available()) {
        log::write(log::Level::Error, "GUI", "framebuffer unavailable for desktop session");
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
    log::write(log::Level::Info, "GUI", "Kurogane Forged Steel desktop session online");

    // The graphical session owns GOP from this point onward. stdout/stderr and
    // diagnostics remain available over serial, but the boot terminal must no
    // longer repaint underneath the compositor.
    terminal::set_framebuffer_output(false);

    windowing::invalidate();
    static_cast<void>(windowing::render_if_needed());
    return true;
}

void flux_session_key(char) {}

void dispatch_desktop_input(const input::Event& event) {
    const windowing::Status status = windowing::dispatch(event);
    if (status != windowing::Status::Ok &&
        status != windowing::Status::NotFound &&
        status != windowing::Status::IterationStopped) {
        log::write(log::Level::Warn, "GUI", windowing::status_message(status));
    }
}

void flux_session_tick(uint64_t) {
    /*
     * main.cpp historically runs the generic Ring-3 slice before its input
     * pump. That was fine for the console, but on a graphical desktop it adds
     * a complete scheduling turn between a physical click and the user
     * process seeing the event. Service the desktop queue here, at the start
     * of the application tick, so input_user_window() has already queued the
     * ABI event before the following Ring-3 slice begins.
     *
     * The later generic input pump remains useful for packets that arrive
     * after this point in the tick. Because this drains the shared queue, an
     * event can only be delivered once.
     */
    static_cast<void>(input::pump());
    input::Event event{};
    while (input::try_read(&event)) {
        dispatch_desktop_input(event);
    }

    // One compositor pass per PIT tick is enough. Input dispatch and Ring-3 UI
    // presents can mark several things dirty inside the same tick; delaying the
    // scanout until here collapses them into one frame instead of repainting
    // the whole desktop once for focus and again for the application's update.
    static_cast<void>(windowing::render_if_needed());
}

void flux_session_stop() {
    terminal::set_framebuffer_output(true);
    log::write(log::Level::Warn, "GUI", "desktop session stopped; terminal display restored");
}

void flux_session_input(const input::Event& event) {
    dispatch_desktop_input(event);
    // Do not force a compositor pass here. Mouse/button bursts can contain
    // many events in one PIT interval and immediate rendering makes each one a
    // full software frame. The tick callback above presents the final state.
}

} // namespace

extern "C" bool kurogane_start_desktop_session() {
    if (applications::running()) {
        return true;
    }

    const applications::Status registration =
        applications::register_application({
            "flux-session",
            "Kurogane Forged Steel userspace desktop session host",
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
