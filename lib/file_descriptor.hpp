/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WAVED_FILE_DESCRIPTOR_HPP
#define WAVED_FILE_DESCRIPTOR_HPP

namespace Waved
{

/** Wrapper around C file descriptors. */
class FileDescriptor
{
public:
    /**
     * Open a file.
     *
     * @param path Path to the file to open.
     * @param flags Opening flags.
     * @throws std::system_error If opening fails.
     */
    FileDescriptor(const char* path, int flags);

    /** Take ownership of an existing file descriptor. */
    FileDescriptor(int fd);

    // Disallow copying input device handles
    FileDescriptor(const FileDescriptor& other) = delete;
    FileDescriptor& operator=(const FileDescriptor& other) = delete;

    // Transfer handle ownership
    FileDescriptor(FileDescriptor&& other) noexcept;
    FileDescriptor& operator=(FileDescriptor&& other) noexcept;

    /** Get the underlying file descriptor. */
    operator int() const;

    /** Close the file. */
    ~FileDescriptor();

private:
    int fd;
}; // class FileDescriptor

} // namespace Waved

#endif // WAVED_FILE_DESCRIPTOR_HPP
