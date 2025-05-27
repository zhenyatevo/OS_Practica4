/*
������� 4: 
������ ����, ��� ������ ��������� ������ ������������ ����������� ������ (shared memory) 
� memory-mapped files ��� ����� ����������� ������������� ������������
*/

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <locale>
#include <codecvt>
#include <Windows.h>

using namespace std;

// ��� ����������� ������
const wchar_t* SHARED_MEMORY_NAME = L"ChatSharedMemory";
// ������ ����������� ������ (���������� ��� ���������)
const int SHARED_MEMORY_SIZE = 1024;

/*
����� ��� ������������� ������ ���� �����
������������ ����������� ������ � ����������� ������
*/
class ChatSync {
private:
    mutex mtx;                      // ������� ��� ������ ����� ������
    condition_variable cv;          // �������� ���������� ��� �������� �������
    bool chat1_turn = true;         // ���� ������� (true - ������� Chat1)
    atomic<bool> running{ true };   // ��������� ���� ������ ���������

public:
    // �������� ����� ������� ��� ���� � ��������� ID
    void wait_for_turn(int chat_id) {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this, chat_id] {
            return !running || (chat_id == 1 && chat1_turn) || (chat_id == 2 && !chat1_turn);
            });
    }

    // ������������ ������� ����� ������
    void switch_turn() {
        lock_guard<mutex> lock(mtx);
        chat1_turn = !chat1_turn;
        cv.notify_all(); // ���������� ��� ��������� ������
    }

    // ��������� ������ ���������
    void stop() {
        lock_guard<mutex> lock(mtx);
        running = false;
        cv.notify_all();
    }

    // �������� ��������� ������
    bool is_running() const {
        return running;
    }

    // ��������, ������� �� ������ Chat1
    bool is_chat1_turn() const {
        return chat1_turn;
    }
};

// ��������� ������� ��� ������ � Unicode (������� �������)
void init_console() {
    SetConsoleOutputCP(CP_UTF8);
    setlocale(LC_ALL, "Russian");
}

// ����� ��� ������ � ����������� �������
class SharedMemory {
private:
    HANDLE hMapFile;
    char* pBuf;

public:
    SharedMemory() : hMapFile(NULL), pBuf(NULL) {
        // ������� ��� ��������� ����������� ������
        hMapFile = CreateFileMapping(
            INVALID_HANDLE_VALUE,    // ���������� ���� ��������
            NULL,                    // �������� ������������ �� ���������
            PAGE_READWRITE,          // ������ �� ������ � ������
            0,                       // ������ (������� DWORD)
            SHARED_MEMORY_SIZE,      // ������ (������� DWORD)
            SHARED_MEMORY_NAME);     // ��� �������

        if (hMapFile == NULL) {
            throw runtime_error("Could not create file mapping object: " + to_string(GetLastError()));
        }

        // �������� ��������� �� ����������� ������
        pBuf = (char*)MapViewOfFile(
            hMapFile,               // ���������� �����������
            FILE_MAP_ALL_ACCESS,    // ������ �� ������ � ������
            0,
            0,
            SHARED_MEMORY_SIZE);

        if (pBuf == NULL) {
            CloseHandle(hMapFile);
            throw runtime_error("Could not map view of file: " + to_string(GetLastError()));
        }
    }

    ~SharedMemory() {
        if (pBuf) UnmapViewOfFile(pBuf);
        if (hMapFile) CloseHandle(hMapFile);
    }

    // ������ ������ � ����������� ������
    void write(const wstring& message) {
        // ������������ wide string � UTF-8
        wstring_convert<codecvt_utf8<wchar_t>> converter;
        string utf8_message = converter.to_bytes(message);

        // �������� ������ � ����������� ������
        memset(pBuf, 0, SHARED_MEMORY_SIZE);
        memcpy(pBuf, utf8_message.c_str(), min(utf8_message.size(), (size_t)SHARED_MEMORY_SIZE - 1));
    }

    // ������ ������ �� ����������� ������
    wstring read() {
        // ������������ UTF-8 ������� � wide string
        wstring_convert<codecvt_utf8<wchar_t>> converter;
        try {
            return converter.from_bytes(pBuf);
        }
        catch (const range_error&) {
            return L"";
        }
    }
};

/*
������� ������ ������ ����
chat_id - ������������� ���� (1 ��� 2)
sync - ������ �������������
shared_mem - ����������� ������
*/
void chat_session(int chat_id, ChatSync& sync, SharedMemory& shared_mem) {
    while (sync.is_running()) {
        // ������� ����� �������
        sync.wait_for_turn(chat_id);
        if (!sync.is_running()) break;

        // ������ ��������� �� ����������� ������
        wstring message_content = shared_mem.read();
        if (!message_content.empty()) {
            // ������ ���������: "ChatX: �����"
            size_t colon_pos = message_content.find(L':');
            if (colon_pos != wstring::npos) {
                wstring sender = message_content.substr(0, colon_pos);
                wstring content = message_content.substr(colon_pos + 2);
                // ������� � �������: "ChatY read ChatX: "�����""
                wcout << L"Chat" << chat_id << L" read " << sender << L": \"" << content << L"\"" << endl;
            }
        }

        // ���� ������ ���� ������� ���������� ���������
        if ((chat_id == 1 && sync.is_chat1_turn()) || (chat_id == 2 && !sync.is_chat1_turn())) {
            wstring message;
            wcout << L"Chat" << chat_id << L": ";
            getline(wcin, message); // ������ ��������� �� ������������

            if (message == L"exit") {
                sync.stop(); // ������� ������
                break;
            }

            // ���������� ��������� � ����������� ������
            shared_mem.write(L"Chat" + to_wstring(chat_id) + L": " + message);
        }

        // ����������� �������
        sync.switch_turn();
        this_thread::sleep_for(chrono::milliseconds(100)); // ��������� �����
    }
}

int main() {
    // ������������� �������
    init_console();

    // �������������� ���������
    wcout << L"=== ��������� ������������ ���� (Shared Memory) ===" << endl;
    wcout << L"���� ����� �� ������� ���������� � �������� ���������" << endl;
    wcout << L"��� ������ ������� 'exit'" << endl << endl;

    try {
        // ������� ������ ����������� ������
        SharedMemory shared_mem;

        // ������� ������ �������������
        ChatSync sync;

        // ��������� ��� ���� � ��������� �������
        thread chat1(chat_session, 1, ref(sync), ref(shared_mem));
        thread chat2(chat_session, 2, ref(sync), ref(shared_mem));

        // ������� ���������� �������
        chat1.join();
        chat2.join();

        wcout << L"��������� ���������." << endl;
    }
    catch (const exception& e) {
        wcerr << L"������: " << e.what() << endl;
        return 1;
    }

    return 0;
}