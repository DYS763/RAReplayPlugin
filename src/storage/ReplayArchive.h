#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <istream>
#include <string>
#include <vector>

namespace rareplay::storage
{
struct ArchiveInfo
{
    std::array<std::uint8_t, 32> mTargetHash{};
    std::vector<std::uint8_t> mMetadata;
    std::uint64_t mStreamBytes = 0;
};

/**
 * 读取并验证完整的 RAReplay V2 容器；帧流以有长度边界的流形式提供。
 * Read、Seek 和 GetStream 共享同一读取位置，位置只相对于帧流区。
 */
class ArchiveReader
{
public:
    ArchiveReader();
    ~ArchiveReader();
    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;
    ArchiveReader(ArchiveReader&&) noexcept;
    ArchiveReader& operator=(ArchiveReader&&) noexcept;

    bool Open(const std::wstring& path, ArchiveInfo& outInfo, std::string& error);
    int Read(void* buffer, int sizeBytes);
    bool Seek(int offset, int origin);
    int GetPosition() const;
    int GetSize() const;
    std::istream& GetStream();
    bool Rewind();
    void Close();

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

/** 只顺序写入帧流；析构只关闭临时文件，不会伪造完成状态。 */
class ArchiveWriter
{
public:
    ArchiveWriter();
    ~ArchiveWriter();
    ArchiveWriter(const ArchiveWriter&) = delete;
    ArchiveWriter& operator=(const ArchiveWriter&) = delete;
    ArchiveWriter(ArchiveWriter&&) noexcept;
    ArchiveWriter& operator=(ArchiveWriter&&) noexcept;

    bool Open(const std::wstring& path, const std::array<std::uint8_t, 32>& targetHash,
        const std::vector<std::uint8_t>& metadata, std::string& error);
    int Write(const void* buffer, int sizeBytes);
    bool Finish(std::string& error);
    void Abort() noexcept;
    int GetSize() const;
    bool Failed() const;

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};
}
