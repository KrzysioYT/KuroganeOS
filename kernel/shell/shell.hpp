#pragma once

namespace shell {

void initialize();
void feed(char character);
void execute(const char* command_line);
void show_prompt();
bool initialized();
const char* current_directory();

} // namespace shell
