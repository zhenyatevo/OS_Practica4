/*
Задание 4: 
версия чата, где вместо файлового обмена используется разделяемая память (shared memory) 
и memory-mapped files для более эффективной межпроцессной коммуникации
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

// Имя разделяемой памяти
const wchar_t* SHARED_MEMORY_NAME = L"ChatSharedMemory";
// Размер разделяемой памяти (достаточно для сообщения)
const int SHARED_MEMORY_SIZE = 1024;

/*
Класс для синхронизации работы двух чатов
Обеспечивает поочередный доступ к разделяемой памяти
*/
class ChatSync {
private:
    mutex mtx;                      // Мьютекс для защиты общих данных
    condition_variable cv;          // Условная переменная для ожидания очереди
    bool chat1_turn = true;         // Флаг очереди (true - очередь Chat1)
    atomic<bool> running{ true };   // Атомарный флаг работы программы

public:
    // Ожидание своей очереди для чата с указанным ID
    void wait_for_turn(int chat_id) {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this, chat_id] {
            return !running || (chat_id == 1 && chat1_turn) || (chat_id == 2 && !chat1_turn);
            });
    }

    // Переключение очереди между чатами
    void switch_turn() {
        lock_guard<mutex> lock(mtx);
        chat1_turn = !chat1_turn;
        cv.notify_all(); // Уведомляем все ожидающие потоки
    }

    // Остановка работы программы
    void stop() {
        lock_guard<mutex> lock(mtx);
        running = false;
        cv.notify_all();
    }

    // Проверка состояния работы
    bool is_running() const {
        return running;
    }

    // Проверка, очередь ли сейчас Chat1
    bool is_chat1_turn() const {
        return chat1_turn;
    }
};

// Настройка консоли для работы с Unicode (русские символы)
void init_console() {
    SetConsoleOutputCP(CP_UTF8);
    setlocale(LC_ALL, "Russian");
}

// Класс для работы с разделяемой памятью
class SharedMemory {
private:
    HANDLE hMapFile;
    char* pBuf;

public:
    SharedMemory() : hMapFile(NULL), pBuf(NULL) {
        // Создаем или открываем разделяемую память
        hMapFile = CreateFileMapping(
            INVALID_HANDLE_VALUE,    // Используем файл подкачки
            NULL,                    // Атрибуты безопасности по умолчанию
            PAGE_READWRITE,          // Доступ на чтение и запись
            0,                       // Размер (старшее DWORD)
            SHARED_MEMORY_SIZE,      // Размер (младшее DWORD)
            SHARED_MEMORY_NAME);     // Имя объекта

        if (hMapFile == NULL) {
            throw runtime_error("Could not create file mapping object: " + to_string(GetLastError()));
        }

        // Получаем указатель на разделяемую память
        pBuf = (char*)MapViewOfFile(
            hMapFile,               // Дескриптор отображения
            FILE_MAP_ALL_ACCESS,    // Доступ на чтение и запись
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

    // Запись строки в разделяемую память
    void write(const wstring& message) {
        // Конвертируем wide string в UTF-8
        wstring_convert<codecvt_utf8<wchar_t>> converter;
        string utf8_message = converter.to_bytes(message);

        // Копируем данные в разделяемую память
        memset(pBuf, 0, SHARED_MEMORY_SIZE);
        memcpy(pBuf, utf8_message.c_str(), min(utf8_message.size(), (size_t)SHARED_MEMORY_SIZE - 1));
    }

    // Чтение строки из разделяемой памяти
    wstring read() {
        // Конвертируем UTF-8 обратно в wide string
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
Функция работы одного чата
chat_id - идентификатор чата (1 или 2)
sync - объект синхронизации
shared_mem - разделяемая память
*/
void chat_session(int chat_id, ChatSync& sync, SharedMemory& shared_mem) {
    while (sync.is_running()) {
        // Ожидаем своей очереди
        sync.wait_for_turn(chat_id);
        if (!sync.is_running()) break;

        // Чтение сообщения из разделяемой памяти
        wstring message_content = shared_mem.read();
        if (!message_content.empty()) {
            // Парсим сообщение: "ChatX: текст"
            size_t colon_pos = message_content.find(L':');
            if (colon_pos != wstring::npos) {
                wstring sender = message_content.substr(0, colon_pos);
                wstring content = message_content.substr(colon_pos + 2);
                // Выводим в формате: "ChatY read ChatX: "текст""
                wcout << L"Chat" << chat_id << L" read " << sender << L": \"" << content << L"\"" << endl;
            }
        }

        // Если сейчас наша очередь отправлять сообщение
        if ((chat_id == 1 && sync.is_chat1_turn()) || (chat_id == 2 && !sync.is_chat1_turn())) {
            wstring message;
            wcout << L"Chat" << chat_id << L": ";
            getline(wcin, message); // Читаем сообщение от пользователя

            if (message == L"exit") {
                sync.stop(); // Команда выхода
                break;
            }

            // Записываем сообщение в разделяемую память
            shared_mem.write(L"Chat" + to_wstring(chat_id) + L": " + message);
        }

        // Переключаем очередь
        sync.switch_turn();
        this_thread::sleep_for(chrono::milliseconds(100)); // Небольшая пауза
    }
}

int main() {
    // Инициализация консоли
    init_console();

    // Приветственное сообщение
    wcout << L"=== Программа поочередного чата (Shared Memory) ===" << endl;
    wcout << L"Чаты будут по очереди отправлять и получать сообщения" << endl;
    wcout << L"Для выхода введите 'exit'" << endl << endl;

    try {
        // Создаем объект разделяемой памяти
        SharedMemory shared_mem;

        // Создаем объект синхронизации
        ChatSync sync;

        // Запускаем два чата в отдельных потоках
        thread chat1(chat_session, 1, ref(sync), ref(shared_mem));
        thread chat2(chat_session, 2, ref(sync), ref(shared_mem));

        // Ожидаем завершения потоков
        chat1.join();
        chat2.join();

        wcout << L"Программа завершена." << endl;
    }
    catch (const exception& e) {
        wcerr << L"Ошибка: " << e.what() << endl;
        return 1;
    }

    return 0;
}