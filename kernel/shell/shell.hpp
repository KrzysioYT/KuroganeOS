#pragma once

namespace shell {

// The framebuffer applications are a frozen diagnostic preview. They are
// exposed only for an explicit boot=desktop session.
void initialize(bool experimental_gui_enabled = false);
void feed(char character);
void execute(const char* command_line);
void show_prompt();
bool initialized();
const char* current_directory();

} // namespace shell
