#pragma once
#include <QString>
#include <cstddef>

class MappedFile {
public:
    MappedFile();
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    bool open(const QString& filePath);
    void close();
    bool isOpen() const;
    const char* data() const;
    size_t size() const;
    QString toQString() const;

private:
    void* m_data = nullptr;
    size_t m_size = 0;
#ifdef _WIN32
    void* m_fileHandle = nullptr;
    void* m_mappingHandle = nullptr;
#else
    int m_fd = -1;
#endif
};
