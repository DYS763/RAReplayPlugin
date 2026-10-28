#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ReplayArchive.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <streambuf>
#include <utility>

namespace rareplay::storage
{
namespace
{
constexpr std::size_t HEADER_BYTES = 96;
constexpr std::size_t METADATA_LIMIT = 16 * 1024 * 1024;
constexpr std::uint64_t STREAM_LIMIT = 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t ARCHIVE_VERSION = 2;
constexpr std::uint32_t FLAG_FINALIZED = 1;
constexpr std::uint64_t HASH_BUFFER_BYTES = 64 * 1024;
constexpr std::uint8_t ARCHIVE_MAGIC[8] = {'R', 'A', 'R', 'E', 'P', 'L', '2', 0};

void SetError(std::string& error, const char* message)
{
    error = message;
}

void SetWindowsError(std::string& error, const char* operation, DWORD errorCode)
{
    error = operation;
    error += " failed (Win32 error ";
    error += std::to_string(errorCode);
    error += ")";
}

bool WriteAll(HANDLE file, const void* data, std::size_t sizeBytes, std::string& error)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t writtenTotal = 0;
    while (writtenTotal < sizeBytes)
    {
        const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(sizeBytes - writtenTotal, MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(file, bytes + writtenTotal, chunkSize, &written, nullptr) || written == 0)
        {
            SetWindowsError(error, "WriteFile", GetLastError());
            return false;
        }
        writtenTotal += written;
    }
    return true;
}

bool ReadExact(HANDLE file, void* data, std::size_t sizeBytes, std::string& error)
{
    auto* bytes = static_cast<std::uint8_t*>(data);
    std::size_t readTotal = 0;
    while (readTotal < sizeBytes)
    {
        const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(sizeBytes - readTotal, MAXDWORD));
        DWORD bytesRead = 0;
        if (!ReadFile(file, bytes + readTotal, chunkSize, &bytesRead, nullptr) || bytesRead == 0)
        {
            SetWindowsError(error, "ReadFile", GetLastError());
            return false;
        }
        readTotal += bytesRead;
    }
    return true;
}

bool SeekFile(HANDLE file, std::uint64_t position, std::string& error)
{
    LARGE_INTEGER target{};
    target.QuadPart = static_cast<LONGLONG>(position);
    if (!SetFilePointerEx(file, target, nullptr, FILE_BEGIN))
    {
        SetWindowsError(error, "SetFilePointerEx", GetLastError());
        return false;
    }
    return true;
}

void StoreU32(std::uint8_t* destination, std::uint32_t value)
{
    for (unsigned int index = 0; index < 4; ++index)
    {
        destination[index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xff);
    }
}

void StoreU64(std::uint8_t* destination, std::uint64_t value)
{
    for (unsigned int index = 0; index < 8; ++index)
    {
        destination[index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xff);
    }
}

std::uint32_t LoadU32(const std::uint8_t* source)
{
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8);
    }
    return value;
}

std::uint64_t LoadU64(const std::uint8_t* source)
{
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index)
    {
        value |= static_cast<std::uint64_t>(source[index]) << (index * 8);
    }
    return value;
}

std::array<std::uint8_t, HEADER_BYTES> EncodeHeader(std::uint32_t metadataBytes, std::uint32_t flags,
    std::uint64_t streamBytes, const std::array<std::uint8_t, 32>& targetHash,
    const std::array<std::uint8_t, 32>& payloadHash)
{
    std::array<std::uint8_t, HEADER_BYTES> header{};
    std::memcpy(header.data(), ARCHIVE_MAGIC, sizeof(ARCHIVE_MAGIC));
    StoreU32(header.data() + 8, ARCHIVE_VERSION);
    StoreU32(header.data() + 12, static_cast<std::uint32_t>(HEADER_BYTES));
    StoreU32(header.data() + 16, metadataBytes);
    StoreU32(header.data() + 20, flags);
    StoreU64(header.data() + 24, streamBytes);
    std::memcpy(header.data() + 32, targetHash.data(), targetHash.size());
    std::memcpy(header.data() + 64, payloadHash.data(), payloadHash.size());
    return header;
}

class Sha256
{
public:
    ~Sha256() { Close(); }

    bool Open(std::string& error)
    {
        NTSTATUS status = BCryptOpenAlgorithmProvider(&mAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status < 0)
        {
            error = "BCryptOpenAlgorithmProvider failed";
            return false;
        }

        DWORD objectBytes = 0;
        DWORD returnedBytes = 0;
        status = BCryptGetProperty(mAlgorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &returnedBytes, 0);
        if (status < 0 || objectBytes == 0)
        {
            error = "BCryptGetProperty(BCRYPT_OBJECT_LENGTH) failed";
            Close();
            return false;
        }
        mObject.resize(objectBytes);

        DWORD hashBytes = 0;
        status = BCryptGetProperty(mAlgorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashBytes), sizeof(hashBytes), &returnedBytes, 0);
        if (status < 0 || hashBytes != 32)
        {
            error = "BCryptGetProperty(BCRYPT_HASH_LENGTH) failed";
            Close();
            return false;
        }
        status = BCryptCreateHash(mAlgorithm, &mHash, mObject.data(), objectBytes, nullptr, 0, 0);
        if (status < 0)
        {
            error = "BCryptCreateHash failed";
            Close();
            return false;
        }
        return true;
    }

    bool Update(const void* data, std::size_t sizeBytes, std::string& error)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        while (sizeBytes != 0)
        {
            const ULONG chunkBytes = static_cast<ULONG>((std::min<std::size_t>)(sizeBytes, ((std::numeric_limits<ULONG>::max)())));
            if (BCryptHashData(mHash, const_cast<PUCHAR>(bytes), chunkBytes, 0) < 0)
            {
                error = "BCryptHashData failed";
                return false;
            }
            bytes += chunkBytes;
            sizeBytes -= chunkBytes;
        }
        return true;
    }

    bool Finish(std::array<std::uint8_t, 32>& outHash, std::string& error)
    {
        if (BCryptFinishHash(mHash, outHash.data(), static_cast<ULONG>(outHash.size()), 0) < 0)
        {
            error = "BCryptFinishHash failed";
            return false;
        }
        Close();
        return true;
    }

    void Close()
    {
        if (mHash != nullptr)
        {
            BCryptDestroyHash(mHash);
            mHash = nullptr;
        }
        if (mAlgorithm != nullptr)
        {
            BCryptCloseAlgorithmProvider(mAlgorithm, 0);
            mAlgorithm = nullptr;
        }
        mObject.clear();
    }

private:
    BCRYPT_ALG_HANDLE mAlgorithm = nullptr;
    BCRYPT_HASH_HANDLE mHash = nullptr;
    std::vector<std::uint8_t> mObject;
};

class BoundedFileStreamBuffer final : public std::streambuf
{
public:
    BoundedFileStreamBuffer(HANDLE file, std::uint64_t payloadStart, std::uint64_t payloadSize)
        : mFile(file), mPayloadStart(payloadStart), mPayloadSize(payloadSize)
    {
        setg(mBuffer.data(), mBuffer.data(), mBuffer.data());
    }

    std::uint64_t GetPosition() const
    {
        return mBufferStart + static_cast<std::uint64_t>(gptr() - eback());
    }

    bool SetPosition(std::uint64_t position)
    {
        if (position > mPayloadSize)
        {
            return false;
        }
        mBufferStart = position;
        setg(mBuffer.data(), mBuffer.data(), mBuffer.data());
        return true;
    }

protected:
    int_type underflow() override
    {
        const std::uint64_t position = GetPosition();
        if (position >= mPayloadSize)
        {
            return traits_type::eof();
        }
        const DWORD readCapacity = static_cast<DWORD>(std::min<std::uint64_t>(mBuffer.size(), mPayloadSize - position));
        LARGE_INTEGER target{};
        target.QuadPart = static_cast<LONGLONG>(mPayloadStart + position);
        if (!SetFilePointerEx(mFile, target, nullptr, FILE_BEGIN))
        {
            return traits_type::eof();
        }
        DWORD bytesRead = 0;
        if (!ReadFile(mFile, mBuffer.data(), readCapacity, &bytesRead, nullptr) || bytesRead == 0)
        {
            return traits_type::eof();
        }
        mBufferStart = position;
        setg(mBuffer.data(), mBuffer.data(), mBuffer.data() + bytesRead);
        return traits_type::to_int_type(*gptr());
    }

    std::streamsize xsgetn(char* destination, std::streamsize requested) override
    {
        if (requested <= 0)
        {
            return 0;
        }
        std::streamsize copied = 0;
        while (copied < requested)
        {
            if (gptr() == egptr() && traits_type::eq_int_type(underflow(), traits_type::eof()))
            {
                break;
            }
            const std::streamsize available = static_cast<std::streamsize>(egptr() - gptr());
        const std::streamsize count = (std::min)(available, requested - copied);
            std::memcpy(destination + copied, gptr(), static_cast<std::size_t>(count));
            gbump(static_cast<int>(count));
            copied += count;
        }
        return copied;
    }

    pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode mode) override
    {
        if ((mode & std::ios_base::in) == 0)
        {
            return pos_type(off_type(-1));
        }
        std::int64_t base = 0;
        if (direction == std::ios_base::cur)
        {
            base = static_cast<std::int64_t>(GetPosition());
        }
        else if (direction == std::ios_base::end)
        {
            base = static_cast<std::int64_t>(mPayloadSize);
        }
        else if (direction != std::ios_base::beg)
        {
            return pos_type(off_type(-1));
        }
        if ((offset < 0 && offset < -base) || (offset > 0 && base > (std::numeric_limits<std::int64_t>::max)() - offset))
        {
            return pos_type(off_type(-1));
        }
        const std::int64_t target = base + offset;
        if (target < 0 || static_cast<std::uint64_t>(target) > mPayloadSize || !SetPosition(static_cast<std::uint64_t>(target)))
        {
            return pos_type(off_type(-1));
        }
        return pos_type(static_cast<off_type>(target));
    }

    pos_type seekpos(pos_type position, std::ios_base::openmode mode) override
    {
        return seekoff(static_cast<off_type>(position), std::ios_base::beg, mode);
    }

private:
    HANDLE mFile;
    std::uint64_t mPayloadStart;
    std::uint64_t mPayloadSize;
    std::uint64_t mBufferStart = 0;
    std::array<char, HASH_BUFFER_BYTES> mBuffer{};
};

bool PathExists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}
}

class ArchiveReader::Impl
{
public:
    ~Impl() { Close(); }

    void Close()
    {
        mStream.reset();
        mStreamBuffer.reset();
        if (mFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(mFile);
            mFile = INVALID_HANDLE_VALUE;
        }
        mStreamBytes = 0;
        mMetadataBytes = 0;
        mPayloadStart = 0;
    }

    HANDLE mFile = INVALID_HANDLE_VALUE;
    std::uint64_t mPayloadStart = 0;
    std::uint64_t mStreamBytes = 0;
    std::uint32_t mMetadataBytes = 0;
    std::unique_ptr<BoundedFileStreamBuffer> mStreamBuffer;
    std::unique_ptr<std::istream> mStream;
};

ArchiveReader::ArchiveReader() : mImpl(std::make_unique<Impl>()) {}
ArchiveReader::~ArchiveReader() = default;
ArchiveReader::ArchiveReader(ArchiveReader&&) noexcept = default;
ArchiveReader& ArchiveReader::operator=(ArchiveReader&&) noexcept = default;

bool ArchiveReader::Open(const std::wstring& path, ArchiveInfo& outInfo, std::string& error)
{
    Close();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        SetWindowsError(error, "CreateFileW", GetLastError());
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < static_cast<LONGLONG>(HEADER_BYTES))
    {
        SetError(error, "Archive is shorter than the fixed header");
        CloseHandle(file);
        return false;
    }

    std::array<std::uint8_t, HEADER_BYTES> header{};
    if (!ReadExact(file, header.data(), header.size(), error))
    {
        CloseHandle(file);
        return false;
    }
    if (std::memcmp(header.data(), ARCHIVE_MAGIC, sizeof(ARCHIVE_MAGIC)) != 0)
    {
        SetError(error, "Archive magic is invalid");
        CloseHandle(file);
        return false;
    }
    const std::uint32_t version = LoadU32(header.data() + 8);
    const std::uint32_t headerBytes = LoadU32(header.data() + 12);
    const std::uint32_t metadataBytes = LoadU32(header.data() + 16);
    const std::uint32_t flags = LoadU32(header.data() + 20);
    const std::uint64_t streamBytes = LoadU64(header.data() + 24);
    if (version != ARCHIVE_VERSION || headerBytes != HEADER_BYTES)
    {
        SetError(error, "Archive version or header size is unsupported");
        CloseHandle(file);
        return false;
    }
    if (metadataBytes == 0 || metadataBytes > METADATA_LIMIT || streamBytes < 32 || streamBytes > STREAM_LIMIT)
    {
        SetError(error, "Archive metadata or frame stream exceeds the format limit");
        CloseHandle(file);
        return false;
    }
    if (flags != FLAG_FINALIZED)
    {
        SetError(error, "Archive is not finalized or contains unsupported flags");
        CloseHandle(file);
        return false;
    }
    const std::uint64_t payloadStart = HEADER_BYTES + static_cast<std::uint64_t>(metadataBytes);
    if (streamBytes > ((std::numeric_limits<std::uint64_t>::max)()) - payloadStart ||
        static_cast<std::uint64_t>(fileSize.QuadPart) != payloadStart + streamBytes)
    {
        SetError(error, "Archive length does not match the header");
        CloseHandle(file);
        return false;
    }

    Sha256 hash;
    if (!hash.Open(error) || !SeekFile(file, HEADER_BYTES, error))
    {
        CloseHandle(file);
        return false;
    }
    std::array<std::uint8_t, HASH_BUFFER_BYTES> buffer{};
    std::uint64_t remaining = static_cast<std::uint64_t>(metadataBytes) + streamBytes;
    while (remaining != 0)
    {
        const DWORD chunkBytes = static_cast<DWORD>((std::min<std::uint64_t>)(buffer.size(), remaining));
        if (!ReadExact(file, buffer.data(), chunkBytes, error) || !hash.Update(buffer.data(), chunkBytes, error))
        {
            CloseHandle(file);
            return false;
        }
        remaining -= chunkBytes;
    }
    std::array<std::uint8_t, 32> actualPayloadHash{};
    if (!hash.Finish(actualPayloadHash, error))
    {
        CloseHandle(file);
        return false;
    }
    if (std::memcmp(actualPayloadHash.data(), header.data() + 64, actualPayloadHash.size()) != 0)
    {
        SetError(error, "Archive payload SHA-256 mismatch");
        CloseHandle(file);
        return false;
    }

    ArchiveInfo info;
    std::memcpy(info.mTargetHash.data(), header.data() + 32, info.mTargetHash.size());
    info.mMetadata.resize(metadataBytes);
    info.mStreamBytes = streamBytes;
    if (!SeekFile(file, HEADER_BYTES, error) || (metadataBytes != 0 && !ReadExact(file, info.mMetadata.data(), metadataBytes, error)) ||
        !SeekFile(file, payloadStart, error))
    {
        CloseHandle(file);
        return false;
    }

    mImpl->mFile = file;
    mImpl->mPayloadStart = payloadStart;
    mImpl->mStreamBytes = streamBytes;
    mImpl->mMetadataBytes = metadataBytes;
    mImpl->mStreamBuffer = std::make_unique<BoundedFileStreamBuffer>(file, payloadStart, streamBytes);
    mImpl->mStream = std::make_unique<std::istream>(mImpl->mStreamBuffer.get());
    outInfo = std::move(info);
    error.clear();
    return true;
}

int ArchiveReader::Read(void* buffer, int sizeBytes)
{
    if (!mImpl || !mImpl->mStream || sizeBytes <= 0 || buffer == nullptr)
    {
        return 0;
    }
    mImpl->mStream->clear();
    mImpl->mStream->read(static_cast<char*>(buffer), sizeBytes);
    return static_cast<int>(mImpl->mStream->gcount());
}

bool ArchiveReader::Seek(int offset, int origin)
{
    if (!mImpl || !mImpl->mStream || !mImpl->mStreamBuffer)
    {
        return false;
    }
    std::ios_base::seekdir direction;
    if (origin == SEEK_SET)
    {
        direction = std::ios_base::beg;
    }
    else if (origin == SEEK_CUR)
    {
        direction = std::ios_base::cur;
    }
    else if (origin == SEEK_END)
    {
        direction = std::ios_base::end;
    }
    else
    {
        return false;
    }
    mImpl->mStream->clear();
    mImpl->mStream->seekg(offset, direction);
    return !mImpl->mStream->fail();
}

int ArchiveReader::GetPosition() const
{
    if (!mImpl || !mImpl->mStreamBuffer)
    {
        return 0;
    }
    return static_cast<int>(mImpl->mStreamBuffer->GetPosition());
}

int ArchiveReader::GetSize() const
{
    return mImpl ? static_cast<int>(mImpl->mStreamBytes) : 0;
}

std::istream& ArchiveReader::GetStream()
{
    static std::istream emptyStream(nullptr);
    if (!mImpl || !mImpl->mStream)
    {
        emptyStream.clear();
        return emptyStream;
    }
    return *mImpl->mStream;
}

bool ArchiveReader::Rewind()
{
    return Seek(0, SEEK_SET);
}

void ArchiveReader::Close()
{
    if (mImpl)
    {
        mImpl->Close();
    }
}

class ArchiveWriter::Impl
{
public:
    ~Impl() { CloseFile(); }

    void CloseFile()
    {
        if (mFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(mFile);
            mFile = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE mFile = INVALID_HANDLE_VALUE;
    std::wstring mPath;
    std::wstring mPartialPath;
    std::array<std::uint8_t, 32> mTargetHash{};
    std::uint32_t mMetadataBytes = 0;
    std::uint64_t mStreamBytes = 0;
    Sha256 mHash;
    bool mFailed = false;
    bool mFinished = false;
};

ArchiveWriter::ArchiveWriter() : mImpl(std::make_unique<Impl>()) {}
ArchiveWriter::~ArchiveWriter() = default;
ArchiveWriter::ArchiveWriter(ArchiveWriter&&) noexcept = default;
ArchiveWriter& ArchiveWriter::operator=(ArchiveWriter&&) noexcept = default;

bool ArchiveWriter::Open(const std::wstring& path, const std::array<std::uint8_t, 32>& targetHash,
    const std::vector<std::uint8_t>& metadata, std::string& error)
{
    Abort();
    if (metadata.empty() || metadata.size() > METADATA_LIMIT)
    {
        SetError(error, "Metadata must be 1..16 MiB");
        return false;
    }
    if (PathExists(path))
    {
        SetError(error, "Final archive already exists; refusing to overwrite");
        return false;
    }

    mImpl->mPath = path;
    mImpl->mPartialPath = path + L".partial";
    mImpl->mFile = CreateFileW(mImpl->mPartialPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (mImpl->mFile == INVALID_HANDLE_VALUE)
    {
        SetWindowsError(error, "CreateFileW(partial)", GetLastError());
        return false;
    }
    mImpl->mTargetHash = targetHash;
    mImpl->mMetadataBytes = static_cast<std::uint32_t>(metadata.size());
    if (!mImpl->mHash.Open(error))
    {
        mImpl->mFailed = true;
        return false;
    }

    const std::array<std::uint8_t, 32> emptyHash{};
    const auto header = EncodeHeader(mImpl->mMetadataBytes, 0, 0, mImpl->mTargetHash, emptyHash);
    if (!WriteAll(mImpl->mFile, header.data(), header.size(), error) ||
        (!metadata.empty() && !WriteAll(mImpl->mFile, metadata.data(), metadata.size(), error)) ||
        !mImpl->mHash.Update(metadata.data(), metadata.size(), error))
    {
        mImpl->mFailed = true;
        return false;
    }
    error.clear();
    return true;
}

int ArchiveWriter::Write(const void* buffer, int sizeBytes)
{
    if (!mImpl || mImpl->mFile == INVALID_HANDLE_VALUE || mImpl->mFailed || mImpl->mFinished || sizeBytes < 0 ||
        (sizeBytes > 0 && buffer == nullptr))
    {
        if (mImpl && (sizeBytes < 0 || (sizeBytes > 0 && buffer == nullptr)))
        {
            mImpl->mFailed = true;
        }
        return 0;
    }
    if (sizeBytes == 0)
    {
        return 0;
    }
    if (static_cast<std::uint64_t>(sizeBytes) > STREAM_LIMIT - mImpl->mStreamBytes)
    {
        mImpl->mFailed = true;
        return 0;
    }

    std::string error;
    if (!WriteAll(mImpl->mFile, buffer, static_cast<std::size_t>(sizeBytes), error) ||
        !mImpl->mHash.Update(buffer, static_cast<std::size_t>(sizeBytes), error))
    {
        mImpl->mFailed = true;
        return 0;
    }
    mImpl->mStreamBytes += static_cast<std::uint64_t>(sizeBytes);
    return sizeBytes;
}

bool ArchiveWriter::Finish(std::string& error)
{
    if (!mImpl || mImpl->mFile == INVALID_HANDLE_VALUE || mImpl->mFailed || mImpl->mFinished)
    {
        SetError(error, "Archive writer is not in a finishable state");
        return false;
    }

    std::array<std::uint8_t, 32> payloadHash{};
    if (!mImpl->mHash.Finish(payloadHash, error))
    {
        mImpl->mFailed = true;
        return false;
    }
    if (mImpl->mStreamBytes < 32)
    {
        SetError(error, "Frame stream is shorter than its header");
        mImpl->mFailed = true;
        return false;
    }
    const auto header = EncodeHeader(mImpl->mMetadataBytes, FLAG_FINALIZED, mImpl->mStreamBytes, mImpl->mTargetHash, payloadHash);
    if (!SeekFile(mImpl->mFile, 0, error) || !WriteAll(mImpl->mFile, header.data(), header.size(), error) || !FlushFileBuffers(mImpl->mFile))
    {
        if (error.empty())
        {
            SetWindowsError(error, "FlushFileBuffers", GetLastError());
        }
        mImpl->mFailed = true;
        return false;
    }
    mImpl->CloseFile();
    if (!MoveFileW(mImpl->mPartialPath.c_str(), mImpl->mPath.c_str()))
    {
        SetWindowsError(error, "MoveFileW(finalize)", GetLastError());
        mImpl->mFailed = true;
        return false;
    }
    mImpl->mFinished = true;
    error.clear();
    return true;
}

void ArchiveWriter::Abort() noexcept
{
    if (mImpl)
    {
        mImpl->CloseFile();
        mImpl->mHash.Close();
        mImpl->mPath.clear();
        mImpl->mPartialPath.clear();
        mImpl->mTargetHash.fill(0);
        mImpl->mMetadataBytes = 0;
        mImpl->mStreamBytes = 0;
        mImpl->mFailed = false;
        mImpl->mFinished = false;
    }
}

int ArchiveWriter::GetSize() const
{
    return mImpl ? static_cast<int>(mImpl->mStreamBytes) : 0;
}

bool ArchiveWriter::Failed() const
{
    return !mImpl || mImpl->mFailed;
}
}
