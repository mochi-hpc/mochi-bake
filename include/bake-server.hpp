/*
 * (C) 2019 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __BAKE_SERVER_HPP
#define __BAKE_SERVER_HPP

#include <string>
#include <vector>
#include <bake.hpp>
#include <bake-server.h>

#define _CHECK_RET(__ret) \
    if (__ret != BAKE_SUCCESS) throw exception(__ret)

namespace bake {

/**
 * @brief Creates a pool at a given path, with a given size and mode.
 *
 * @param pool_name Pool name.
 * @param pool_size Pool size.
 * @param pool_mode Mode.
 */
inline void create_raw_target(const std::string& path, size_t size)
{
    int ret = bake_create_raw_target(path.c_str(), size);
    _CHECK_RET(ret);
}

/**
 * @brief The provider class is the C++ equivalent to a bake_provider_t.
 */
class provider {

    margo_instance_id m_mid      = MARGO_INSTANCE_NULL;
    bake_provider_t   m_provider = NULL;

    provider(margo_instance_id              mid,
             uint16_t                       provider_id,
             const bake_provider_init_info* args)
        : m_mid(mid)
    {
        int ret = bake_provider_register(mid, provider_id, args, &m_provider);
        _CHECK_RET(ret);
    }

    static void finalize_callback(void* args)
    {
        auto* p = static_cast<provider*>(args);
        delete p;
    }

  public:
    /**
     * @brief Factory method to create an instance of provider.
     *
     * @param mid Margo instance id.
     * @param provider_id Provider id.
     * @param pool Argobots pool.
     * @param config JSON config.
     * @param abtid ABT-IO instance.
     * @param remi REMI provider.
     *
     * @return Pointer to newly created provider.
     */
    static provider* create(margo_instance_id  mid,
                            uint16_t           provider_id = 0,
                            ABT_pool           pool        = ABT_POOL_NULL,
                            const std::string& config      = "{}",
                            abt_io_instance_id abtio = ABT_IO_INSTANCE_NULL,
                            void*              remi_provider = NULL,
                            void*              remi_client   = NULL)
    {
        bake_provider_init_info args = BAKE_PROVIDER_INIT_INFO_INITIALIZER;
        args.json_config             = config.c_str();
        args.rpc_pool                = pool;
        args.aid                     = abtio;
        args.remi_provider           = remi_provider;
        args.remi_client             = remi_client;
        return create(mid, provider_id, &args);
    }

    /**
     * @brief Factory method to create an instance of provider.
     *
     * @param mid Margo instance id.
     * @param provider_id Provider id.
     * @param args bake_provider_init_info structure.
     *
     * @return Pointer to newly created provider.
     */
    static provider* create(margo_instance_id              mid,
                            uint16_t                       provider_id,
                            const bake_provider_init_info* args = nullptr)
    {
        auto p = new provider(mid, provider_id, args);
        margo_provider_push_finalize_callback(mid, p, &finalize_callback, p);
        return p;
    }

    /**
     * @brief Deleted copy constructor.
     */
    provider(const provider&) = delete;

    /**
     * @brief Deleted move constructor.
     */
    provider(provider&& other) = delete;

    /**
     * @brief Deleted copy-assignment operator.
     */
    provider& operator=(const provider&) = delete;

    /**
     * @brief Deleted move-assignment operator.
     */
    provider& operator=(provider&& other) = delete;

    /**
     * @brief Destructor.
     */
    ~provider()
    {
        margo_provider_pop_finalize_callback(m_mid, this);
        bake_provider_deregister(m_provider);
    }

    /**
     * @brief Adds a storage target to the provider.
     * The target must have been created beforehand.
     *
     * @param target_name Path to the target.
     *
     * @return a target object.
     */
    target attach_target(const std::string& target_name)
    {
        target t;
        int ret = bake_provider_attach_target(m_provider, target_name.c_str(),
                                              &(t.m_tid));
        _CHECK_RET(ret);
        return t;
    }

    /**
     * @brief Create a storage target and attach it to the provider.
     *
     * @param target_name Path to the target.
     * @param size Target size.
     *
     * @return a target object.
     */
    target create_target(const std::string& target_name, size_t size)
    {
        target t;
        int ret = bake_provider_create_target(m_provider, target_name.c_str(),
                                              size, &(t.m_tid));
        _CHECK_RET(ret);
        return t;
    }

    /**
     * @brief Removes the storage target from the provider.
     * This does not removes the storage target from the device, it
     * simply makes it unaccessible through this provider.
     *
     * @param t target to remove.
     */
    void detach_target(const target& t)
    {
        int ret = bake_provider_detach_target(m_provider, t.m_tid);
        _CHECK_RET(ret);
    }

    /**
     * @brief Removes all the storage targets managed by the provider.
     */
    void detach_all_targets()
    {
        int ret = bake_provider_detach_all_targets(m_provider);
        _CHECK_RET(ret);
    }

    /**
     * @brief Count the number of storage targets managed by the provider.
     *
     * @return number of storage targets.
     */
    uint64_t count_targets() const
    {
        uint64_t count;
        int      ret = bake_provider_count_targets(m_provider, &count);
        _CHECK_RET(ret);
        return count;
    }

    /**
     * @brief Lists all the storage targets managed by the provider.
     *
     * @return Vector of targets.
     */
    std::vector<target> list_targets() const
    {
        uint64_t                      count = count_targets();
        std::vector<target>           result(count);
        std::vector<bake_target_id_t> tgts(count);
        int ret = bake_provider_list_targets(m_provider, tgts.data());
        _CHECK_RET(ret);
        for (unsigned i = 0; i < count; i++) { result[i].m_tid = tgts[i]; }
        return result;
    }

    std::string get_config() const
    {
        char* cfg = bake_provider_get_config(m_provider);
        if (!cfg) return std::string();
        auto str_cfg = std::string(cfg);
        free(cfg);
        return str_cfg;
    }
};

} // namespace bake

#undef _CHECK_RET

#endif
