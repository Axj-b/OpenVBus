
#include <windows.h>
#include <string>
#include <iostream>

static bool send_cmd(const std::string &cmd, std::string &resp) {
    HANDLE h = CreateFileW(L"\\\\.\\pipe\\vbusd", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    std::string line    = cmd + "\n";
    DWORD       written = 0;
    if (!WriteFile(h, line.data(), (DWORD)line.size(), &written, NULL)) {
        CloseHandle(h);
        return false;
    }
    char  buf[4096];
    DWORD read = 0;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &read, NULL)) {
        CloseHandle(h);
        return false;
    }
    buf[read] = 0;
    resp.assign(buf, buf + read);
    CloseHandle(h);
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: vbusctl <command...>\n";
        return 1;
    }
    std::string cmd;
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            cmd.push_back(' ');
        cmd += argv[i];
    }
    std::string resp;
    if (!send_cmd(cmd, resp)) {
        std::cerr << "Failed to contact vbusd\n";
        return 2;
    }
    std::cout << resp;
    return 0;
}
