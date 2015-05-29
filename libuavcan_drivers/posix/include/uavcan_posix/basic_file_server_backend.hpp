/****************************************************************************
*
*   Copyright (c) 2015 PX4 Development Team. All rights reserved.
*      Author: Pavel Kirienko <pavel.kirienko@gmail.com>
*              David Sidrane <david_s5@usa.net>
*
****************************************************************************/

#ifndef UAVCAN_POSIX_BASIC_FILE_SERVER_BACKEND_HPP_INCLUDED
#define UAVCAN_POSIX_BASIC_FILE_SERVER_BACKEND_HPP_INCLUDED

#include <sys/stat.h>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>

#include <uavcan/data_type.hpp>
#include <uavcan/protocol/file/Error.hpp>
#include <uavcan/protocol/file/EntryType.hpp>
#include <uavcan/protocol/file/Read.hpp>
#include <uavcan/protocol/file_server.hpp>
#include <uavcan/data_type.hpp>

namespace uavcan_posix
{
/**
 * This interface implements a POSIX compliant IFileServerBackend interface
 */
class BasicFileSeverBackend : public uavcan::IFileServerBackend
{
    enum { FilePermissions = 438 };   ///< 0o666

protected:
    /**
     * Back-end for uavcan.protocol.file.GetInfo.
     * Implementation of this method is required.
     * On success the method must return zero.
     */
    virtual int16_t getInfo(const Path& path, uint64_t& out_crc64, uint32_t& out_size, EntryType& out_type)
    {
        PROBE(4, true);
        int rv = uavcan::protocol::file::Error::INVALID_VALUE;
        FileCRC crc;
        if (path.size() > 0)
        {
            using namespace std;
            out_size = 0;
            out_crc64 = 0;

            rv = -ENOENT;
            uint8_t buffer[512];

            PROBE_MARK(4);
            int fd = ::open(path.c_str(), O_RDONLY);
            PROBE_MARK(4);

            if (fd >= 0)
            {
                int len = 0;

                do
                {

                    PROBE_MARK(4);
                    len = ::read(fd, buffer, sizeof(buffer));
                    PROBE_MARK(4);

                    if (len > 0)
                    {

                        out_size += len;
                        crc.add(buffer, len);

                    }
                    else if (len < 0)
                    {
                        rv = EIO;
                        goto out_close;
                    }

                }
                while(len > 0);

                out_crc64 = crc.get();

                // We can assume the path is to a file and the file is readable.
                out_type.flags = uavcan::protocol::file::EntryType::FLAG_READABLE |
                                 uavcan::protocol::file::EntryType::FLAG_FILE;

                // TODO Using fixed flag FLAG_READABLE until we add file permission checks to return actual value.
                // TODO Check whether the object pointed by path is a file or a directory
                // On could ad call to stat() to determine if the path is to a file or a directory but the
                // what are the return parameters in this case?

                rv = 0;
            out_close:
                PROBE_MARK(4);
                close(fd);
                PROBE_MARK(4);
            }
        }
        PROBE(4, false);
        return rv;
    }

    /**
     * Back-end for uavcan.protocol.file.Read.
     * Implementation of this method is required.
     * @ref inout_size is set to @ref ReadSize; read operation is required to return exactly this amount, except
     * if the end of file is reached.
     * On success the method must return zero.
     */

    Path last_path;
    int last_fd;
    virtual int16_t read(const Path& path, const uint32_t offset, uint8_t* out_buffer, uint16_t& inout_size)
    {
        PROBE(5, true);
        int rv = uavcan::protocol::file::Error::INVALID_VALUE;

        if (path.size() > 0)
        {
            int fd = -1;

            if (last_fd != -1 &&
                last_path.size() != 0 &&
                0 == strcmp(path.c_str(), last_path.c_str()))
            {

                fd = last_fd;

            }
            else
            {

                if (last_fd != -1)
                {
                    (void)close(last_fd);
                    last_fd = -1;
                }

                last_path = path;
                PROBE(4, true);
                fd = open(path.c_str(), O_RDONLY);
                PROBE(4, false);
                if (fd >= 0)
                {
                    last_fd = fd;
                }
                PROBE(4, false);
            }

            if (fd < 0)
            {
                rv = errno;
            }
            else
            {
                PROBE(4, true);
                rv = ::lseek(fd, offset, SEEK_SET);
                PROBE(4, false);

                if (rv < 0)
                {
                    rv = errno;
                }
                else
                {
                    // TODO use a read at offset to fill on EAGAIN
                    PROBE(4, true);
                    ssize_t len = ::read(fd, out_buffer, inout_size);
                    PROBE(4, false);

                    if (len < 0)
                    {
                        rv = errno;
                    }
                    else
                    {

                        inout_size = len;
                        rv = 0;
                    }
                }
                if (rv == 0 && inout_size == 0)
                {
                  PROBE(4, true);
                  (void)close(fd);
                  last_path.clear();
                  last_fd = -1;
                  PROBE(4, false);
                }
            }
        }
        PROBE(5, false);
        PROBE(4, false);
        return rv;
    }

public:
    BasicFileSeverBackend() :
        last_fd(-1)
    { }
};

}

#endif // Include guard
