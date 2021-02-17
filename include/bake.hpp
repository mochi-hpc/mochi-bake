/*
 * (C) 2019 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __BAKE_HPP
#define __BAKE_HPP

#include <exception>
#include <string>
#include <bake.h>

namespace bake {

/**
 * @brief Array of error messages for bake::exception.
 */
const char* const bake_error_messages[] = {
    "",
    "Allocation error",
    "Invalid argument",
    "Mercury error",
    "Argobots error",
    "PMEM error",
    "Unknown target",
    "Unknown provider",
    "Unknown region",
    "Access out of bound",
    "REMI error",
    "Operation not supported",
    "Forbidden operation",
    "Unknown backend type",
    "Backend I/O error",
    "Entry does not exist",
    "Entry already exists",
    "Not enough memory"
};

class client;
class provider;

/**
 * @brief target instances are equivalent to bake_target_id_t.
 */
class target {

    friend class client;
    friend class provider;

    bake_target_id_t m_tid;

    public:

    /**
     * @brief Constructor.
     */
    target()                         = default;

    /**
     * @brief Constructor from a human-readable string.
     * The string must have been obtained using target's
     * std::string cast operator.
     *
     * @param str String representation of the target.
     */
    target(const std::string& str) {
        bake_target_id_from_string(str.c_str(), &m_tid);
    }

    /**
     * @brief Copy constructor.
     */
    target(const target&)            = default;

    /**
     * @brief Move constructor.
     */
    target(target&&)                 = default;

    /**
     * @brief Copy-assignment operator.
     */
    target& operator=(const target&) = default;

    /**
     * @brief Move-assignment operator.
     */
    target& operator=(target&&)      = default;

    /**
     * @brief Destructor.
     */
    ~target()                        = default;

    /**
     * @brief Converts the target to a human-readable string.
     */
    operator std::string() const {
        char str[37];
        bake_target_id_to_string(m_tid, str, 37);
        return std::string(str);
    }

    /**
     * @brief Serialization function (useful when using Thallium).
     *
     * @tparam A Archive type.
     * @param ar Archive object.
     */
    template<typename A>
    void save(A& ar) const {
        ar.write(&m_tid);
    }

    /**
     * @brief Deserialization function (useful when using Thallium).
     *
     * @tparam A Archive type.
     * @param ar Archive object.
     */
    template<typename A>
    void load(A& ar) {
        ar.read(&m_tid);
    }
};

/**
 * @brief The region class is the C++ equivalent to bake_region_id_t.
 */
class region {

    friend class client;

    bake_region_id_t m_rid;

    public:

    /**
     * @brief Default constructor.
     */
    region() = default;

    /**
     * @brief Builds a region from a human-readable string representation.
     *
     * @param str String representation of the region.
     */
    region(const std::string& str) {
        bake_region_id_from_string(str.c_str(), &m_rid);
    }

    /**
     * @brief Copy constructor.
     */
    region(const region&)            = default;

    /**
     * @brief Move constructor.
     */
    region(region&&)                 = default;

    /**
     * @brief Copy-assignment operator.
     */
    region& operator=(const region&) = default;

    /**
     * @brief Move-assignment operator.
     */
    region& operator=(region&&)      = default;

    /**
     * @brief Destructor.
     */
    ~region()                        = default;

    /**
     * @brief Converts the region into a human-readable string.
     *
     * @return Human-readable representation of the region.
     */
    operator std::string() const {
        char str[128];
        bake_region_id_to_string(m_rid, str, 128);
        return std::string(str);
    }

    /**
     * @brief Serialization function (useful when using Thallium).
     *
     * @tparam A Archive type.
     * @param ar Archive object.
     */
    template<typename A>
    void save(A& ar) const {
        ar.write(&m_rid);
    }

    /**
     * @brief Deserialization function (useful when using Thallium).
     *
     * @tparam A Archive type.
     * @param ar Archive object.
     */
    template<typename A>
    void load(A& ar) {
        ar.read(&m_rid);
    }
};

/**
 * @brief Exception thrown by Bake methods.
 */
class exception : public std::exception {

    int m_error;
    std::string m_msg;

    public:

    exception(int error)
    : m_error(error) {
        if(error < 0 && error > BAKE_ERR_END) {
            m_msg = std::string("[BAKE] ") + bake_error_messages[-error];
        } else {
            m_msg = std::string("[BAKE] Unknown error code "+std::to_string(error));
        }
    }

    const char* what() const noexcept override {
        return m_msg.c_str();
    }

    int error() const {
        return m_error;
    }
};

}

#endif
