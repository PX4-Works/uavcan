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

    class FDCacheBase {

    public:
        FDCacheBase() { }
        virtual ~FDCacheBase() { }

        virtual int open(const char *path, int oflags)
        {
            using namespace std;

            return ::open(path, oflags);
        }

        virtual int close(int fd)
        {
            using namespace std;

            return ::close(fd);
        }

    };

    FDCacheBase fallback_;

    class FDCache : public FDCacheBase {

        class FDCacheItem {

            friend FDCache;
            FDCacheItem *next_;
            int refcnt_;
            int fd_;
            int oflags_;
            const char *path_;

        public:

            enum { InvalidFD = -1 };

            FDCacheItem() :
                next_(0),
                refcnt_(0),
                fd_(InvalidFD),
                oflags_(0),
                path_(0)
            { }

            FDCacheItem(int fd, const char * path, int oflags) :
                next_(0),
                refcnt_(0),
                fd_(fd),
                oflags_(oflags),
                path_(strdup(path))
            {

            }

            ~FDCacheItem()
            {
                if (valid())
                {
                    delete path_;
                }
            }

            inline bool valid()
            {
                return path_ != 0;
            }

            inline int getFD()
            {
                return fd_;
            }

            inline int getRefCount()
            {
                return refcnt_;
            }

            int addRef()
            {
                refcnt_++;
                return getRefCount();
            }

            int removeRef()
            {
                refcnt_--;
                return getRefCount();
            }

            int compare(const char * path, int oflags)
            {
                return (oflags_ == oflags && 0 == ::strcmp(path, path_)) ? 0 : 1;
            }

            int compare(int fd)
            {
                return fd_ == fd ? 0 : 1;
            }

        };

        FDCacheItem* head_;

        FDCacheItem* find(const char *path, int oflags)
        {
            FDCacheItem* pi = head_;
            while(pi && 0 != pi->compare(path, oflags))
            {
                pi = pi->next_;
            }
            return pi;
        }

        FDCacheItem* find(int fd)
        {
            FDCacheItem* pi = head_;
            while(pi && 0 != pi->compare(fd))
            {
                pi = pi->next_;
            }
            return pi;
        }


        FDCacheItem* add(FDCacheItem* pi)
        {
            pi->next_ = head_;
            head_ = pi;
            pi->addRef();
            return pi;
        }

        FDCacheItem* remove(FDCacheItem* pi)
        {
            if (pi->removeRef() > 0 )
            {
                pi = 0;
            }
            else
            {

                FDCacheItem** li = &head_;
                FDCacheItem* i = head_;
                while(i)
                {
                    if (i == pi)
                    {
                        *li = pi->next_;
                        break;
                    }
                    li = &(i->next_);
                    i = i->next_;
                }
            }
            return pi;
        }

    public:

        FDCache() :
            head_(0)
        { }

        virtual ~FDCache()
        {
            for(FDCacheItem* pi = head_; pi; )
            {
                FDCacheItem* tmp = pi->next_;
                (void)FDCacheBase::close(pi->fd_);
                delete pi;
                pi = tmp;

            }
        }

        virtual int open(const char *path, int oflags)
        {
            int fd = FDCacheItem::InvalidFD;

            FDCacheItem *pi = find(path, oflags);

            if (pi != 0)
            {
                pi->addRef();
            }
            else
            {
                fd = FDCacheBase::open(path, oflags);
                if (fd < 0)
                {
                    return fd;
                }

                /* Allocate and clone path */

                pi = new FDCacheItem(fd, path, oflags);

                /* Allocation worked but check clone */

                if (pi && !pi->valid())
                {

                    /* Allocation worked but clone or path failed */

                    delete pi;
                    pi = 0;
                }

                if (pi == 0)
                {
                    /*
                     * If allocation fails no harm just can not cache it
                     * return open fd
                     */

                    return fd;
                }
                /* add new */
                add(pi);
            }
            return pi->getFD();
        }


        virtual int close(int fd)
        {
            FDCacheItem *pi = find(fd);
            if (pi == 0)
            {
                /*
                 * If not found just close it
                 */

                return FDCacheBase::close(fd);
            }
            pi = remove(pi);
            if (pi)
            {
                delete pi;
                return FDCacheBase::close(fd);
            }
            return 0;
        }

    };


    FDCacheBase *fdcache_;

    FDCacheBase&  getFDCache()
    {
        if (fdcache_ == 0)
        {
            fdcache_ = new FDCache();

            if (fdcache_ == 0)
            {
                fdcache_ = &fallback_;

            }
        }
        return *fdcache_;
    }

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
                (void)::close(fd);
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
    __attribute__((optimize("O0")))
    virtual int16_t read(const Path& path, const uint32_t offset, uint8_t* out_buffer, uint16_t& inout_size)
    {
        PROBE(5, true);
        int rv = uavcan::protocol::file::Error::INVALID_VALUE;

        if (path.size() > 0)
        {
            FDCacheBase& cache = getFDCache();
            PROBE(4, true);
            int fd = cache.open(path.c_str(), O_RDONLY);
            PROBE(4, false);

            if (fd < 0)
            {
                rv = errno;
            }
            else
            {
                PROBE(4, true);
                rv = ::lseek(fd, offset, SEEK_SET);
                PROBE(4, false);

                ssize_t len = 0;

                if (rv < 0)
                {
                    rv = errno;
                }
                else
                {
                    // TODO use a read at offset to fill on EAGAIN
                    PROBE(4, true);
                    len = ::read(fd, out_buffer, inout_size);
                    PROBE(4, false);

                    if (len < 0)
                    {
                        rv = errno;
                    }
                    else
                    {
                        rv = 0;
                    }
                }

                if (rv != 0 || len != inout_size)
                {
                    PROBE(4, true);
                    (void)cache.close(fd);
                    PROBE(4, false);
                }

                inout_size = len;
            }
        }
        PROBE(5, false);
        PROBE(4, false);
        return rv;
    }

public:

    BasicFileSeverBackend() :
        fdcache_(0)
    {
    }

    ~BasicFileSeverBackend()
    {
        if (fdcache_ != &fallback_)
        {
            delete fdcache_;
            fdcache_ = 0;
        }
    }
};

}

#endif // Include guard
