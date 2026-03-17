#include "MappedFile.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

MappedFile::MappedFile() = default;

MappedFile::~MappedFile() {
    close();
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : m_data(other.m_data)
    , m_size(other.m_size)
#ifdef _WIN32
    , m_fileHandle(other.m_fileHandle)
    , m_mappingHandle(other.m_mappingHandle)
#else
    , m_fd(other.m_fd)
#endif
{
    other.m_data = nullptr;
    other.m_size = 0;
#ifdef _WIN32
    other.m_fileHandle = nullptr;
    other.m_mappingHandle = nullptr;
#else
    other.m_fd = -1;
#endif
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        close();
        m_data = other.m_data;
        m_size = other.m_size;
#ifdef _WIN32
        m_fileHandle = other.m_fileHandle;
        m_mappingHandle = other.m_mappingHandle;
        other.m_fileHandle = nullptr;
        other.m_mappingHandle = nullptr;
#else
        m_fd = other.m_fd;
        other.m_fd = -1;
#endif
        other.m_data = nullptr;
        other.m_size = 0;
    }
    return *this;
}

bool MappedFile::open(const QString& filePath) {
    close();

#ifdef _WIN32
    // Open file
    m_fileHandle = CreateFileW(
        reinterpret_cast<LPCWSTR>(filePath.utf16()),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        m_fileHandle = nullptr;
        return false;
    }

    // Get file size
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(m_fileHandle, &fileSize)) {
        CloseHandle(m_fileHandle);
        m_fileHandle = nullptr;
        return false;
    }

    m_size = static_cast<size_t>(fileSize.QuadPart);

    // Empty file: valid open but no mapping needed
    if (m_size == 0) {
        return true;
    }

    // Create file mapping
    m_mappingHandle = CreateFileMappingW(
        m_fileHandle,
        nullptr,
        PAGE_READONLY,
        0, 0,
        nullptr
    );
    if (!m_mappingHandle) {
        CloseHandle(m_fileHandle);
        m_fileHandle = nullptr;
        m_size = 0;
        return false;
    }

    // Map view
    m_data = MapViewOfFile(m_mappingHandle, FILE_MAP_READ, 0, 0, 0);
    if (!m_data) {
        CloseHandle(m_mappingHandle);
        m_mappingHandle = nullptr;
        CloseHandle(m_fileHandle);
        m_fileHandle = nullptr;
        m_size = 0;
        return false;
    }

    return true;

#else
    // Linux/macOS
    QByteArray pathBytes = filePath.toUtf8();
    m_fd = ::open(pathBytes.constData(), O_RDONLY);
    if (m_fd < 0) {
        m_fd = -1;
        return false;
    }

    struct stat st;
    if (fstat(m_fd, &st) != 0) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_size = static_cast<size_t>(st.st_size);

    // Empty file
    if (m_size == 0) {
        return true;
    }

    m_data = mmap(nullptr, m_size, PROT_READ, MAP_PRIVATE, m_fd, 0);
    if (m_data == MAP_FAILED) {
        m_data = nullptr;
        ::close(m_fd);
        m_fd = -1;
        m_size = 0;
        return false;
    }

    return true;
#endif
}

void MappedFile::close() {
#ifdef _WIN32
    if (m_data) {
        UnmapViewOfFile(m_data);
        m_data = nullptr;
    }
    if (m_mappingHandle) {
        CloseHandle(m_mappingHandle);
        m_mappingHandle = nullptr;
    }
    if (m_fileHandle) {
        CloseHandle(m_fileHandle);
        m_fileHandle = nullptr;
    }
#else
    if (m_data && m_size > 0) {
        munmap(m_data, m_size);
        m_data = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
    m_size = 0;
}

bool MappedFile::isOpen() const {
#ifdef _WIN32
    return m_fileHandle != nullptr;
#else
    return m_fd >= 0;
#endif
}

const char* MappedFile::data() const {
    return static_cast<const char*>(m_data);
}

size_t MappedFile::size() const {
    return m_size;
}

QString MappedFile::toQString() const {
    return QString::fromUtf8(static_cast<const char*>(m_data), static_cast<int>(m_size));
}
