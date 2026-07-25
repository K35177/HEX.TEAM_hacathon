#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

std::wstring quote_argument(const std::wstring& argument) {
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;
    std::wstring quoted(1, L'"');
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            quoted.append(backslashes * 2U + 1U, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2U, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::filesystem::path executable_path() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                              static_cast<DWORD>(buffer.size()));
        if (size == 0) return {};
        if (size < buffer.size() - 1U) return std::filesystem::path(buffer.data(), buffer.data() + size);
        buffer.resize(buffer.size() * 2U);
    }
}

std::filesystem::path find_on_path(const wchar_t* name) {
    const DWORD required = SearchPathW(nullptr, name, nullptr, 0, nullptr, nullptr);
    if (required == 0) return {};
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1U);
    if (SearchPathW(nullptr, name, nullptr, static_cast<DWORD>(buffer.size()),
                    buffer.data(), nullptr) == 0) return {};
    return buffer.data();
}

int show_error(const wchar_t* message) {
    MessageBoxW(nullptr, message, L"Acoustic File Transfer", MB_OK | MB_ICONERROR);
    return 1;
}

int run_client() {
    const auto launcher = executable_path();
    if (launcher.empty()) return show_error(L"Не удалось определить путь к приложению.");
    const auto directory = launcher.parent_path();
    const auto backend = directory / L"acoustic-transfer.exe";
    if (!std::filesystem::is_regular_file(backend)) {
        return show_error(L"Рядом с клиентом не найден acoustic-transfer.exe.");
    }

    const std::vector<std::filesystem::path> scripts{
        directory / L"ui" / L"acoustic_ui.py",
        directory.parent_path() / L"ui" / L"acoustic_ui.py",
        std::filesystem::current_path() / L"ui" / L"acoustic_ui.py"};
    std::filesystem::path script;
    for (const auto& candidate : scripts) {
        if (std::filesystem::is_regular_file(candidate)) {
            script = std::filesystem::absolute(candidate);
            break;
        }
    }
    if (script.empty()) return show_error(L"Не найден ui/acoustic_ui.py.");

    auto interpreter = find_on_path(L"pythonw.exe");
    DWORD creation_flags = 0;
    if (interpreter.empty()) {
        interpreter = find_on_path(L"python.exe");
        creation_flags = CREATE_NO_WINDOW;
    }
    if (interpreter.empty()) {
        return show_error(L"Нужен Python 3 с Tkinter. Python не найден в PATH.");
    }

    std::wstring command = quote_argument(interpreter.wstring()) + L" " +
        quote_argument(script.wstring()) + L" --binary " + quote_argument(backend.wstring());
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    auto working_directory = script.parent_path().parent_path().wstring();
    if (!CreateProcessW(interpreter.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                        creation_flags, nullptr, working_directory.c_str(), &startup, &process)) {
        return show_error(L"Не удалось запустить графический интерфейс.");
    }
    CloseHandle(process.hThread);
    const DWORD wait_result = WaitForSingleObject(process.hProcess, 1500U);
    if (wait_result == WAIT_OBJECT_0) {
        DWORD exit_code = 1;
        (void)GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(process.hProcess);
        return exit_code == 0 ? 0 : show_error(L"Графический интерфейс завершился с ошибкой.");
    }
    CloseHandle(process.hProcess);
    return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return run_client();
}

#else

#include <cstdlib>

namespace {
std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (const char character : value) result += character == '\'' ? "'\\''" : std::string(1, character);
    return result + '\'';
}
}

int main(int argc, char** argv) {
    const auto launcher = std::filesystem::absolute(argc > 0 ? argv[0] : "acoustic-client");
    const auto backend = launcher.parent_path() / "acoustic-transfer";
    if (!std::filesystem::is_regular_file(backend)) return 1;
    return std::system((shell_quote(backend.string()) + " ui").c_str()) == 0 ? 0 : 1;
}

#endif
