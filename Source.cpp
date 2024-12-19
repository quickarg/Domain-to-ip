#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <thread>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <vector>
#include <regex>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

// ���������� ���������� ��� ����������
static HWND hEditDomain;  // ���� ����� ������
static HWND hButton;      // ������
static HWND hStaticIP;    // ���� ��� ������ ���������� (IP)

static boost::asio::io_service io_service;
static std::unique_ptr<boost::asio::ip::tcp::resolver> resolverPtr;
static std::thread ioThread;
static bool stopIo = false;
static std::unique_ptr<boost::asio::io_service::work> keepWork; // ������ ��� "���������" io_service �����

enum DomainErrorType {
    MISSING_TOP_LEVEL_DOMAIN,
    INVALID_CHARACTERS,
    EMPTY_OR_SPACES,
    INVALID_LENGTH,
    UNSUPPORTED_CHARACTERS,
    OTHER_ERRORS
};

// �������� ������
const char* DomainErrorDescriptions[] = {
    "����������� ����� �������� ������.",            // MISSING_TOP_LEVEL_DOMAIN
    "������������ ������������ �������.",           // INVALID_CHARACTERS
    "����� ������ ��� �������� ������ �������.",    // EMPTY_OR_SPACES
    "�������� ����� ������.",                       // INVALID_LENGTH
    "���������������� ������� ��� punycode.",       // UNSUPPORTED_CHARACTERS
    "������ ������, ��������� � �������."            // OTHER_ERRORS
};

// �������� ������
std::vector<DomainErrorType> validateDomain(const std::string& domain) {
    std::vector<DomainErrorType> errors;

    // ������� ��������� � �������� �������
    std::string trimmedDomain = domain;
    trimmedDomain.erase(0, trimmedDomain.find_first_not_of(" \t"));
    trimmedDomain.erase(trimmedDomain.find_last_not_of(" \t") + 1);

    // ��������� (c): �������� �� ������ ������
    if (trimmedDomain.empty()) {
        errors.push_back(EMPTY_OR_SPACES);
        return errors;
    }

    // ��������� (d): �������� ����� ������
    if (trimmedDomain.length() < 3 || trimmedDomain.length() > 253) {
        errors.push_back(INVALID_LENGTH);
    }

    // ��������� (b): �������� ������������ ��������
    std::regex invalidCharsRegex("[^a-zA-Z0-9.-]");
    if (std::regex_search(trimmedDomain, invalidCharsRegex)) {
        errors.push_back(INVALID_CHARACTERS);
    }

    // ��������� (a): �������� �� ������� ������ �������� ������
    if (trimmedDomain.find('.') == std::string::npos ||
        trimmedDomain.back() == '.') {
        errors.push_back(MISSING_TOP_LEVEL_DOMAIN);
    }

    // ��������� (e): �������� ��������, ���������������� punycode
    if (!std::all_of(trimmedDomain.begin(), trimmedDomain.end(), [](unsigned char c) {
        return (c >= 32 && c <= 126); // ���������� ������� ASCII
        })) {
        errors.push_back(UNSUPPORTED_CHARACTERS);
    }

    // ��������� (f): ������ ������ (��������, �������� �� "..")
    if (trimmedDomain.find("..") != std::string::npos) {
        errors.push_back(OTHER_ERRORS);
    }

    return errors;
}


// ����� ������
std::string getErrorMessages(const std::vector<DomainErrorType>& errors) {
    std::string errorMessages;
    for (const auto& error : errors) {
        errorMessages += DomainErrorDescriptions[error];
        errorMessages += "\n";
    }
    return errorMessages.empty() ? "No errors found." : errorMessages;
}

void handle_resolve(const boost::system::error_code& err,
    boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{
    if (err) {
        std::string errorMsg = "Error: " + err.message();
        SetWindowTextA(hStaticIP, errorMsg.c_str());
        return;
    }

    std::string resultIPs;
    boost::asio::ip::tcp::resolver::iterator end;
    for (; endpoint_iterator != end; ++endpoint_iterator) {
        boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
        resultIPs += endpoint.address().to_string();
        resultIPs += "\r\n";
    }

    if (resultIPs.empty()) {
        resultIPs = "No IP found.";
    }

    SetWindowTextA(hStaticIP, resultIPs.c_str());
}

void start_resolve(const std::string& host)
{
    if (!resolverPtr) {
        resolverPtr = std::make_unique<boost::asio::ip::tcp::resolver>(io_service);
    }

    boost::asio::ip::tcp::resolver::query query(host, "http");
    resolverPtr->async_resolve(query, boost::bind(&handle_resolve,
        boost::placeholders::_1,
        boost::placeholders::_2));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
    {
        hEditDomain = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            10, 10, 300, 25,
            hwnd, (HMENU)101, GetModuleHandle(NULL), NULL);
        hButton = CreateWindowA("BUTTON", "Resolve",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            320, 10, 80, 25,
            hwnd, (HMENU)102, GetModuleHandle(NULL), NULL);
        hStaticIP = CreateWindowA("STATIC", "",
            WS_CHILD | WS_VISIBLE,
            10, 50, 390, 200,
            hwnd, (HMENU)103, GetModuleHandle(NULL), NULL);
    }
    break;
    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 102) {
            char domain[256];
            GetWindowTextA(hEditDomain, domain, 256);
            std::string host = domain;
            if (!host.empty()) {
                // �������� ��������� ������
                std::vector<DomainErrorType> errors = validateDomain(host);
                if (!errors.empty()) {
                    // ���� ���� ������, ������� �� � ���� ��� IP
                    SetWindowTextA(hStaticIP, getErrorMessages(errors).c_str());
                    return 0;
                }

                // ���� ������ ���, �������� ������� ���������� ������
                SetWindowTextA(hStaticIP, "Resolving...");
                io_service.post([host]() {
                    start_resolve(host);
                    });
            }
            else {
                SetWindowTextA(hStaticIP, "Please enter a domain.");
            }

        }
    }
    break;
    case WM_DESTROY:
        stopIo = true;
        io_service.stop();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(WNDCLASSEXA));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "DnsWindowClass";

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    HWND hwnd = CreateWindowExA(
        0,
        "DnsWindowClass",
        "DNS Resolver",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, // ����� ����� ����
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 300,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) {
        MessageBoxA(NULL, "Window Creation Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    keepWork = std::make_unique<boost::asio::io_service::work>(io_service);
    ioThread = std::thread([]() {
        io_service.run();
        });
    // �������� ���� ���������
    MSG Msg;
    while (GetMessageA(&Msg, NULL, 0, 0) > 0) {
        TranslateMessage(&Msg);
        DispatchMessageA(&Msg);
    }
    io_service.stop();
    if (ioThread.joinable()) {
        ioThread.join();
    }

    return (int)Msg.wParam;
}
