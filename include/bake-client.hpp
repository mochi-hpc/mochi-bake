/*
 * (C) 2019 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */
#ifndef __BAKE_CLIENT_HPP
#define __BAKE_CLIENT_HPP

#include <string>
#include <vector>
#include <bake.hpp>
#include <bake-client.h>

namespace bake {

#define _CHECK_RET(__ret) \
    if(__ret != BAKE_SUCCESS) throw exception(__ret)

class provider_handle;

/**
 * @brief The client class is the C++ equivalent to bake_client_t.
 */
class client {

    bake_client_t m_client = BAKE_CLIENT_NULL;
    bool          m_owns_client = true;

    friend class provider_handle;

    public:

    /**
     * @brief Default constructor, will make an invalid client.
     */
    client() = default;

    /**
     * @brief Builds a bake client using a margo instance.
     *
     * @param mid Margo instance id.
     */
    client(margo_instance_id mid) {
        int ret = bake_client_init(mid, &m_client);
        _CHECK_RET(ret);
    }

    /**
     * @brief Builds a client object from a bake_client_t.
     * If transfer_ownership is true, the newly created instance
     * will be responsible for destroying the bake_client_t instance
     * in its destructor.
     *
     * @param c bake_client_t instance.
     * @param transfer_ownership whether to transfer ownership to the object.
     */
    client(bake_client_t c, bool transfer_ownership=false)
    : m_client(c)
    , m_owns_client(transfer_ownership) {}

    /**
     * @brief Deleted copy constructor.
     */
    client(const client&) = delete;

    /**
     * @brief Move constructor.
     */
    client(client&& c)
    : m_client(c.m_client)
    , m_owns_client(c.m_owns_client) {
        c.m_client = BAKE_CLIENT_NULL;
        c.m_owns_client = false;
    }

    /**
     * @brief Deleted copy-assignment operator.
     */
    client& operator=(const client&) = delete;

    /**
     * @brief Move-assignment operator.
     */
    client& operator=(client&& c) {
        if(&c == this) return *this;
        if(m_client != BAKE_CLIENT_NULL && m_owns_client) {
            int ret = bake_client_finalize(m_client);
            _CHECK_RET(ret);
        }
        m_client = c.m_client;
        m_owns_client = c.m_owns_client;
        c.m_client = BAKE_CLIENT_NULL;
        c.m_owns_client = false;
        return *this;
    }

    /**
     * @brief Destructor.
     */
    ~client() {
        if(m_client != BAKE_CLIENT_NULL && m_owns_client)
            bake_client_finalize(m_client);
    }

    /**
     * @brief Cast to bool. Will return true only if
     * the client is valid.
     */
    operator bool() const {
        return m_client != BAKE_CLIENT_NULL;
    }

    /**
     * @brief Sends an RPC to get the list of available
     * targets on a provider.
     *
     * @param ph Provider handle.
     * @param max_targets Maximum number of targets to return.
     * If this number is set to 0, the function will send multiple
     * RPCs requesting an increasingly larger number of targets
     * until it gets all the targets managed by the provider.
     */
    std::vector<target> probe(
            const provider_handle& ph,
            uint64_t max_targets=0) const;

    /**
     * @brief Creates a region of a given size on the target.
     *
     * @param ph Provider handle.
     * @param tgt Target.
     * @param region_size Size of the region.
     *
     * @return a region object corresponding to the newly created region.
     */
    region create(
            const provider_handle& ph,
            const target& tid,
            uint64_t region_size) const;

    /**
     * @brief Writes to a region that has been created.
     * The offset is relative to the region. If bake we compiled
     * with --enable-sizecheck, this method will throw an exception
     * if the access gets past the region extents.
     *
     * @param ph Provider handle.
     * @param tid Target to write to.
     * @param rid Region to write to.
     * @param region_offset Offset in the region at which to start.
     * @param buf Buffer with the data to write.
     * @param buf_size Size of the data to write.
     */
    void write(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t region_offset,
            void const *buf,
            uint64_t buf_size) const;

    /**
     * @brief Writes data into a region using a bulk handle
     * (equivalent to bake_proxy_write).
     *
     * @param ph Provider handle.
     * @param tid Target to write to.
     * @param rid Region to write to.
     * @param region_offset Offset in the region.
     * @param remote_bulk Bulk handle to pull the data from.
     * @param remote_offset Offset in the bulk handle.
     * @param remote_addr Address of the process owning the data.
     * @param size Size of the data to transfer.
     *
     * Note: if remote_addr is an empty string, the provider
     * will assume that the bulk handle belongs to the send.
     */
    void write(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t region_offset,
            hg_bulk_t remote_bulk,
            uint64_t remote_offset,
            const std::string& remote_addr,
            uint64_t size) const;

    /**
     * @brief Persist a segment of a given region.
     *
     * @param ph Provider handle.
     * @param tid Target.
     * @param rid Region.
     * @param offset Offset.
     * @param size Size.
     */
    void persist(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t offset,
            size_t size) const;

    /**
     * @brief Creates, writes, and persists a region in a single RPC.
     *
     * @param ph Provider handle.
     * @param tid Target in which to create the region.
     * @param buf Buffer containing the data to write.
     * @param buf_size Buffer size.
     *
     * @return region instance corresponding to the newly created region.
     */
    region create_write_persist(
            const provider_handle& ph,
            const target& tid,
            void const *buf,
            size_t buf_size) const;

    /**
     * @brief Creates, writes, and persists a region in a single RPC,
     * using a bulk handle instead of a pointer to local data.
     *
     * @param ph Provider handle.
     * @param tid Target in which to create the region.
     * @param remote_bulk Bulk handle representing the data to write.
     * @param remote_offset Offset in the bulk handle.
     * @param remote_addr Remote address from where the bulk handle comes.
     * @param size Size of the data to transfer.
     *
     * @return region instance corresponding to the newly created region.
     */
    region create_write_persist(
            const provider_handle& ph,
            const target& tid,
            hg_bulk_t remote_bulk,
            uint64_t remote_offset,
            const std::string& remote_addr,
            size_t size) const;

    /**
     * @brief Gets the size of a region. This call will work only if bake
     * was compiled with --enable-sizecheck, and it is not the recommended
     * to rely on this mechanism to track the size of regions.
     *
     * @param ph Provider handle.
     * @param tid Target.
     * @param rid Region.
     *
     * @return Size of the region.
     */
    size_t get_size(
            const provider_handle& ph,
            const target& tid,
            const region& rid) const;

    /**
     * @brief If the client and the provider live in the same process,
     * this call can be used to translate a region id into an actual pointer.
     *
     * @param ph Provider handle.
     * @param tid Target.
     * @param rid Region.
     *
     * @return A pointer to the actual memory pointed by the region.
     */
    void* get_data(
            const provider_handle& ph,
            const target& tid,
            const region& rid) const;

    /**
     * @brief Reads the provided region at a given offset.
     *
     * @param ph Provider handle.
     * @param tid Target to read from.
     * @param rid Region to read from.
     * @param region_offset Offset in the region.
     * @param buffer Buffer where to place the read data.
     * @param buf_size Buffer size.
     *
     * @return The amount of data read.
     */
    size_t read(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t region_offset,
            void* buffer,
            size_t buf_size) const;

    /**
     * @brief Reads a given region and pushed the data into a bulk handle.
     *
     * @param ph Provider handle.
     * @param tid Target to read from.
     * @param rid Region id.
     * @param region_offset Offset in the region at which to read.
     * @param remote_bulk Bulk handle to which to push the data.
     * @param remote_offset Offset in the bulk handle.
     * @param remote_addr Remote address.
     * @param size Size to transfer.
     *
     * @return Size read.
     */
    size_t read(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t region_offset,
            hg_bulk_t remote_bulk,
            uint64_t remote_offset,
            const std::string& remote_addr,
            size_t size) const;

    /**
     * @brief Removes the region from its target.
     *
     * @param ph Provider handle.
     * @param tid Target containing the region to remove.
     * @param rid Region to remove.
     */
    void remove(
            const provider_handle& ph,
            const target& tid,
            const region& rid) const;

    /**
     * @brief Migrate a region from a target to another.
     *
     * @param soure Source provider.
     * @param source_tid Source target.
     * @param source_rid Source region.
     * @param region_size Region size.
     * @param remove_source Whether to remove the source region.
     * @param dest_addr Destination address.
     * @param dest_provider_id Destination provider id.
     * @param dest_tid Destination target.
     *
     * @return The resuling region.
     */
    region migrate(
            const provider_handle& soure,
            const target& source_tid,
            const region& source_rid,
            size_t region_size,
            bool remove_source,
            const std::string& dest_addr,
            uint16_t dest_provider_id,
            const target& dest_tid) const;
            
    /**
     * @brief Migrates a full target from a source provider
     * to a destination provider.
     *
     * @param soure Source provider.
     * @param tid Target to migrate.
     * @param remove_source Whether to remove the source.
     * @param dest_addr Destination address.
     * @param dest_provider_id Destination provider id.
     * @param dest_root Path on the destination.
     */
    void migrate(
            const provider_handle& soure,
            const target& tid,
            bool remove_source,
            const std::string& dest_addr,
            uint16_t dest_provider_id,
            const std::string& dest_root) const;


    /**
     * @brief Shuts down the margo instance at the given address.
     *
     * @param addr Address of the margo instance to shut down.
     */
    void shutdown(hg_addr_t addr) const {
        int ret = bake_shutdown_service(m_client, addr);
        _CHECK_RET(ret);
    }
};

/**
 * @brief The provider_handle class is the C++ equivalent of
 * bake_provider_handle_t.
 */
class provider_handle {

    friend class client;

    bake_provider_handle_t m_ph = BAKE_PROVIDER_HANDLE_NULL;

    public:

    /**
     * @brief Default constructor. Will build an invalid provider handle.
     */
    provider_handle() = default;

    /**
     * @brief Buils a valid provider handle from a client, an
     * address, and a provider id.
     *
     * @param clt Client instance.
     * @param addr Address of the provider.
     * @param provider_id Provider id.
     */
    provider_handle(
            const client& clt,
            hg_addr_t addr,
            uint16_t provider_id=0) {
        int ret = bake_provider_handle_create(clt.m_client,
                addr, provider_id, &m_ph);
        _CHECK_RET(ret);
    }

    /**
     * @brief Copy constructor.
     */
    provider_handle(const provider_handle& other) 
    : m_ph(other.m_ph) {
        if(m_ph != BAKE_PROVIDER_HANDLE_NULL) {
            int ret = bake_provider_handle_ref_incr(m_ph);
            _CHECK_RET(ret);
        }
    }

    /**
     * @brief Move constructor.
     */
    provider_handle(provider_handle&& other)
    : m_ph(other.m_ph) {
        other.m_ph = BAKE_PROVIDER_HANDLE_NULL;
    }

    /**
     * @brief Copy-assignment operator.
     */
    provider_handle& operator=(const provider_handle& other) {
        if(&other == this) return *this;
        if(m_ph != BAKE_PROVIDER_HANDLE_NULL) {
            int ret = bake_provider_handle_release(m_ph);
            _CHECK_RET(ret);
        }
        m_ph = other.m_ph;
        int ret = bake_provider_handle_ref_incr(m_ph);
        _CHECK_RET(ret);
        return *this;
    }

    /**
     * @brief Move-assignment operator.
     */
    provider_handle& operator=(provider_handle& other) {
        if(&other == this) return *this;
        if(m_ph != BAKE_PROVIDER_HANDLE_NULL) {
            int ret = bake_provider_handle_release(m_ph);
            _CHECK_RET(ret);
        }
        m_ph = other.m_ph;
        other.m_ph = BAKE_PROVIDER_HANDLE_NULL;
        return *this;
    }

    /**
     * @brief Destructor.
     */
    ~provider_handle() {
        if(m_ph != BAKE_PROVIDER_HANDLE_NULL)
            bake_provider_handle_release(m_ph);
    }

    /**
     * @brief Get the size below which bake will pass
     * the data into the RPC arguments.
     *
     * @return The eager size limit.
     */
    uint64_t get_eager_limit() const {
        uint64_t result;
        int ret = bake_provider_handle_get_eager_limit(m_ph, &result);
        _CHECK_RET(ret);
        return result;
    }

    /**
     * @brief Sets the eager size limit.
     */
    void set_eager_limit(uint64_t limit) {
        int ret = bake_provider_handle_set_eager_limit(m_ph, limit);
        _CHECK_RET(ret);
    }
};

inline std::vector<target> client::probe(
        const provider_handle& ph,
        uint64_t max_targets) const {
    std::vector<bake_target_id_t> tids(max_targets);
    uint64_t num_tgts;
    int ret;
    if(max_targets != 0) {
        ret = bake_probe(ph.m_ph, max_targets, tids.data(), &num_tgts);
        _CHECK_RET(ret);
        tids.resize(num_tgts);
    } else {
        tids.resize(1);
        max_targets = 1;
        while(true) {
            ret = bake_probe(ph.m_ph, max_targets, tids.data(), &num_tgts);
            _CHECK_RET(ret);
            if(num_tgts <  max_targets)
                break;
            max_targets *= 2;
            tids.resize(max_targets);
        }
    }
    std::vector<target> result(num_tgts);
    for(unsigned i=0; i < num_tgts; i++) {
        result[i].m_tid = tids[i];
    }
    return result;
}

inline region client::create(
            const provider_handle& ph,
            const target& tgt,
            uint64_t region_size) const {
    region r;
    int ret = bake_create(ph.m_ph, tgt.m_tid, region_size, &(r.m_rid));
    _CHECK_RET(ret);
    return r;
}

inline void client::write(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t region_offset,
            void const *buf,
            uint64_t buf_size) const {
    int ret = bake_write(ph.m_ph, tid.m_tid, rid.m_rid, region_offset, buf, buf_size);
    _CHECK_RET(ret);
}

inline void client::write(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t region_offset,
            hg_bulk_t remote_bulk,
            uint64_t remote_offset,
            const std::string& remote_addr,
            uint64_t size) const {
    int ret = bake_proxy_write(
            ph.m_ph,
            tid.m_tid,
            rid.m_rid,
            region_offset,
            remote_bulk,
            remote_offset,
            remote_addr.c_str(),
            size);
    _CHECK_RET(ret);
}

inline void client::persist(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t offset,
            size_t size) const {
    int ret = bake_persist(
            ph.m_ph,
            tid.m_tid,
            rid.m_rid,
            offset,
            size);
    _CHECK_RET(ret);
}

inline region client::create_write_persist(
            const provider_handle& ph,
            const target& tid,
            void const *buf,
            size_t buf_size) const {
    region r;
    int ret = bake_create_write_persist(
            ph.m_ph,
            tid.m_tid,
            buf,
            buf_size,
            &r.m_rid);
    _CHECK_RET(ret);
    return r;
}

inline region client::create_write_persist(
            const provider_handle& ph,
            const target& tid,
            hg_bulk_t remote_bulk,
            uint64_t remote_offset,
            const std::string& remote_addr,
            size_t size) const {
    region r;
    int ret = bake_create_write_persist_proxy(
            ph.m_ph,
            tid.m_tid,
            remote_bulk,
            remote_offset,
            remote_addr.c_str(),
            size,
            &(r.m_rid));
    _CHECK_RET(ret);
    return r;
}

inline size_t client::get_size(
            const provider_handle& ph,
            const target& tid,
            const region& rid) const {
    size_t size;
    int ret = bake_get_size(ph.m_ph, tid.m_tid, rid.m_rid, &size);
    _CHECK_RET(ret);
    return size;
}

inline void* client::get_data(
            const provider_handle& ph,
            const target& tid,
            const region& rid) const {
    void* ptr;
    int ret = bake_get_data(ph.m_ph, tid.m_tid, rid.m_rid, &ptr);
    _CHECK_RET(ret);
    return ptr;
}

inline size_t client::read(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t region_offset,
            void* buffer,
            size_t buf_size) const {
    size_t bytes_read;
    int ret = bake_read(
            ph.m_ph,
            tid.m_tid,
            rid.m_rid,
            region_offset,
            buffer,
            buf_size,
            &bytes_read);
    _CHECK_RET(ret);
    return bytes_read;
}

inline size_t client::read(
            const provider_handle& ph,
            const target& tid,
            const region& rid,
            uint64_t region_offset,
            hg_bulk_t remote_bulk,
            uint64_t remote_offset,
            const std::string& remote_addr,
            size_t size) const {
    size_t bytes_read;
    int ret = bake_proxy_read(
            ph.m_ph,
            tid.m_tid,
            rid.m_rid,
            region_offset,
            remote_bulk,
            remote_offset,
            remote_addr.c_str(),
            size,
            &bytes_read);
    _CHECK_RET(ret);
    return bytes_read;
}

inline region client::migrate(
            const provider_handle& source,
            const target& source_tid,
            const region& source_rid,
            size_t region_size,
            bool remove_source,
            const std::string& dest_addr,
            uint16_t dest_provider_id,
            const target& dest_tid) const {
    region r;
    int ret = bake_migrate_region(source.m_ph,
            source_tid.m_tid,
            source_rid.m_rid,
            region_size,
            remove_source,
            dest_addr.c_str(),
            dest_provider_id,
            dest_tid.m_tid,
            &(r.m_rid));
    _CHECK_RET(ret);
    return r;
}
            
inline void client::migrate(
            const provider_handle& source,
            const target& tid,
            bool remove_source,
            const std::string& dest_addr,
            uint16_t dest_provider_id,
            const std::string& dest_root) const {
    int ret = bake_migrate_target(
            source.m_ph,
            tid.m_tid,
            remove_source,
            dest_addr.c_str(),
            dest_provider_id,
            dest_root.c_str());
    _CHECK_RET(ret);
}

inline void client::remove(
            const provider_handle& ph,
            const target& tid,
            const region& rid) const {
    int ret = bake_remove(ph.m_ph, tid.m_tid, rid.m_rid);
    _CHECK_RET(ret);
}

}

#undef _CHECK_RET

#endif
