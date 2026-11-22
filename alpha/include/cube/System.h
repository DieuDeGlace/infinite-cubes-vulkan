#pragma once
#include <windows.h>
#include <iphlpapi.h>
#include <memory>
#include <string>
#include <vector>
#include "abstr/Machine.h"

namespace cube {

// ReadAdapters' room for the network adapters' records.
enum {
    AdapterCapacity = 32
};

// System exposes script callbacks for files, MD5 hashing, network adapters, volume serial numbers
// and the executable path. Script values are 32-bit integers or strings, so the byte buffers, file
// handles and adapter records reach the script as small ids (1-based; 0 is none, and openFile gives
// -1 for a file it cannot open), never as pointers: that keeps the script working in x64 builds.
// An unknown or freed id reads as empty and is otherwise ignored.
class System {
public:
    explicit System(bool test);
    ~System();
    System(const System &) = delete;
    System &operator=(const System &) = delete;

    int newBuffer();                                                // "button:click"
    int loadBuffer(std::string path);                               // "button:hover"
    int bufferFromString(std::string text);                         // "edit:enter"
    void deleteBuffer(int buffer);                                  // "edit:leave"
    void setBufferByte(int buffer, int index, int value);           // "scroll:up"
    int bufferByte(int buffer, int index);                          // "scroll:down"
    int bufferSize(int buffer);                                     // "scroll:click"
    void resizeBuffer(int buffer, int size);                        // "window:resize"
    void appendInt(int buffer, int value);                          // "window:maximize"
    int readAdapters();                                             // "window:close"
    int firstAdapter();                                             // "window:minimize"
    int nextAdapter(int adapter);                                   // "screen:start"
    int adapterType(int adapter);                                   // "screen:options"
    int adapterAddressByte(int adapter, int index);                 // "screen:system"
    std::string fileHash(std::string path);                         // "screen:save"
    std::string stringHash(std::string text);                       // "checkbox:mark"
    std::string bufferHash(int buffer);                             // "checkbox:unmark"
    int openFile(std::string path);                                 // "static:enable"
    void closeFile(int file);                                       // "static:disable"
    int creationTimeHigh(int file);                                 // "static:1"
    int creationTimeLow(int file);                                  // "static:2"
    int accessTimeHigh(int file);                                   // "static:3"
    int accessTimeLow(int file);                                    // "static:4"
    int writeTimeHigh(int file);                                    // "static:5"
    int writeTimeLow(int file);                                     // "static:7"
    std::string modulePath();                                       // "widget:enter"
    std::string volumeSerial();                                     // "widget:leave"
    std::string concatenate(std::string first, std::string second); // "widget:show"
    void setTexts(std::string first, std::string second);           // "widget:hide"
    void setText(std::string text);                                 // "widget:enable"
    std::string bufferString(int buffer);                           // "widget:close"
    int equal(std::string first, std::string second);               // "widget:open"

    abstr::Machine m_machine;
    std::string m_firstText;                                   // setTexts' first
    std::string m_secondText;                                  // setTexts' second
    std::string m_text;                                        // setText
    IP_ADAPTER_INFO m_adapters[AdapterCapacity];               // readAdapters; adapter id i + 1 is m_adapters[i]
    std::vector<std::unique_ptr<std::vector<char>>> m_buffers; // buffer id i + 1; null once deleted
    std::vector<HANDLE> m_files;                               // file id i + 1; INVALID_HANDLE_VALUE once closed
    bool m_test; // the constructor's argument, passed to the script's "test"

private:
    // The buffer, file handle or adapter record an id names; null (INVALID_HANDLE_VALUE) for none.
    std::vector<char> *buffer(int id);
    HANDLE file(int id) const;
    const IP_ADAPTER_INFO *adapter(int id) const;
    int addBuffer(std::unique_ptr<std::vector<char>> data);
    // One of a file's times (creation 0, access 1, write 2), zero when the id names no open file.
    FILETIME fileTime(int id, int which) const;
};

}
