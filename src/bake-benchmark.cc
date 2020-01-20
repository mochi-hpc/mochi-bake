#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <map>
#include <functional>
#include <memory>
#include <mpi.h>
#include <json/json.h>
#include <bake-client.hpp>
#include <bake-server.hpp>


int getConfigInt(Json::Value& config, const std::string& key, int _default) {
    if(config[key]) return config[key].asInt();
    else {
        config[key] = _default;
        return _default;
    }
}

int getConfigBool(Json::Value& config, const std::string& key, bool _default) {
    if(config[key]) return config[key].asBool();
    else {
        config[key] = _default;
        return _default;
    }
}

template<typename T>
class BenchmarkRegistration;

/**
 * The AbstractBenchmark class describes an interface that a benchmark object
 * needs to satisfy. This interface has a setup, execute, and teardown
 * methods. AbstractBenchmark also acts as a factory to create concrete instances.
 */
class AbstractBenchmark {

    MPI_Comm              m_comm;    // communicator gathering all clients
    margo_instance_id     m_mid;     // margo instance
    bake::client&         m_client;  // bake client
    bake::provider_handle m_bake_ph; // provider handle
    bake::target          m_target;  // bake target

    template<typename T>
    friend class BenchmarkRegistration;

    using benchmark_factory_function = std::function<
        std::unique_ptr<AbstractBenchmark>(Json::Value&, MPI_Comm, margo_instance_id, bake::client&, const bake::provider_handle&, const bake::target&)>;
    static std::map<std::string, benchmark_factory_function> s_benchmark_factories;

    protected:

    margo_instance_id mid() { return m_mid; }
    bake::client& client() { return m_client; }
    bake::provider_handle& ph() { return m_bake_ph; }
    bake::target& target() { return m_target; }
    MPI_Comm comm() const { return m_comm; }

    public:

    AbstractBenchmark(MPI_Comm c, margo_instance_id mid, bake::client& client, const bake::provider_handle& ph, const bake::target& tgt)
    : m_comm(c)
    , m_mid(mid)
    , m_client(client)
    , m_bake_ph(ph)
    , m_target(tgt) {}

    virtual ~AbstractBenchmark() = default;
    virtual void setup()    = 0;
    virtual void execute()  = 0;
    virtual void teardown() = 0;

    /**
     * @brief Factory function used to create benchmark instances.
     */
    template<typename ... T>
    static std::unique_ptr<AbstractBenchmark> create(const std::string& type, T&& ... args) {
        auto it = s_benchmark_factories.find(type);
        if(it == s_benchmark_factories.end())
            throw std::invalid_argument(type+" benchmark type unknown");
        return (it->second)(std::forward<T>(args)...);
    }
};

/**
 * @brief The mechanism bellow is used to provide the REGISTER_BENCHMARK macro,
 * which registers a child class of AbstractBenchmark and allows AbstractBenchmark::create("type", ...)
 * to return an instance of this concrete child class.
 */
template<typename T>
class BenchmarkRegistration {
    public:
    BenchmarkRegistration(const std::string& type) {
        AbstractBenchmark::s_benchmark_factories[type] = 
            [](Json::Value& config, MPI_Comm comm, margo_instance_id mid, bake::client& clt, const bake::provider_handle& ph, const bake::target& tgt) {
                return std::make_unique<T>(config, comm, mid, clt, ph, tgt);
        };
    }
};

std::map<std::string, AbstractBenchmark::benchmark_factory_function> AbstractBenchmark::s_benchmark_factories;

#define REGISTER_BENCHMARK(__name, __class) \
    static BenchmarkRegistration<__class> __class##_registration(__name)

class AbstractAccessBenchmark : public AbstractBenchmark {
    
    protected:

    uint64_t                  m_num_entries = 0;
    std::pair<size_t, size_t> m_region_size_range;
    bool                      m_erase_on_teardown;

    public:

    template<typename ... T>
    AbstractAccessBenchmark(Json::Value& config, T&& ... args)
    : AbstractBenchmark(std::forward<T>(args)...) {
        // read the configuration
        m_num_entries = getConfigInt(config, "num-entries", 1);
        if(config["region-sizes"].isIntegral()) {
            auto x = config["region-sizes"].asUInt64();
            m_region_size_range = { x, x+1 };
        } else if(config["region-sizes"].isArray() && config["region-sizes"].size() == 2) {
            auto x = config["region-sizes"][0].asUInt64();
            auto y = config["region-sizes"][1].asUInt64();
            if(x > y) throw std::range_error("invalid key-sizes range");
            m_region_size_range = { x, y };
        } else {
            throw std::range_error("invalid region-sizes range or value");
        }
        m_erase_on_teardown = getConfigBool(config, "erase-on-teardown", true);
        size_t eager_size = getConfigInt(config, "eager-limit", 2048);
        ph().set_eager_limit(eager_size);
    }
};

/**
 * CreateBenchmark executes a series of bake_create operations and measures their duration.
 */
class CreateBenchmark : public AbstractAccessBenchmark {

    protected:

    std::vector<size_t>       m_region_sizes;
    std::vector<bake::region> m_region_ids;

    public:

    template<typename ... T>
    CreateBenchmark(T&& ... args)
    : AbstractAccessBenchmark(std::forward<T>(args)...) { }

    virtual void setup() override {
        // generate key/value pairs and store them in the local
        m_region_ids.resize(m_num_entries);
        m_region_sizes.resize(m_num_entries);
        for(unsigned i=0; i < m_num_entries; i++) {
            size_t size = m_region_size_range.first + (rand() % (m_region_size_range.second - m_region_size_range.first));
            m_region_sizes[i] = size;
        }
    }

    virtual void execute() override {
        // execute create operations
        auto& _clt = client();
        auto& _tgt = target();
        auto& _ph = ph();
        for(unsigned i=0; i < m_num_entries; i++) {
            m_region_ids[i] = _clt.create(_ph, _tgt, m_region_sizes[i]);
        }
    }

    virtual void teardown() override {
        if(m_erase_on_teardown) {
            auto& _clt = client();
            auto& _tgt = target();
            auto& _ph = ph();
            for(unsigned i=0; i < m_num_entries; i++) {
                _clt.remove(_ph, _tgt, m_region_ids[i]);
            }
        }
        m_region_sizes.resize(0); m_region_sizes.shrink_to_fit();
        m_region_ids.resize(0);   m_region_ids.shrink_to_fit();
    }
};
REGISTER_BENCHMARK("create", CreateBenchmark);

/**
 * CreateWritePersistBenchmark executes a series of bake_create_write_persist
 * operations and measures their duration.
 */
class CreateWritePersistBenchmark : public AbstractAccessBenchmark {

    protected:

    std::vector<size_t>       m_region_sizes;
    std::vector<bake::region> m_region_ids;
    std::vector<char>         m_data;
    bool                      m_reuse_buffer;
    bool                      m_preregister_bulk;
    hg_bulk_t                 m_bulk = HG_BULK_NULL;

    public:

    template<typename ... T>
    CreateWritePersistBenchmark(Json::Value& config, T&& ... args)
    : AbstractAccessBenchmark(config, std::forward<T>(args)...) { 
        m_reuse_buffer = getConfigBool(config, "reuse-buffer", false);
        m_preregister_bulk = getConfigBool(config, "preregister-bulk", false);
    }

    virtual void setup() override {
        // generate region sizes
        size_t data_size = 0;
        m_region_ids.resize(m_num_entries);
        m_region_sizes.resize(m_num_entries);
        for(unsigned i=0; i < m_num_entries; i++) {
            size_t size = m_region_size_range.first + (rand() % (m_region_size_range.second - m_region_size_range.first));
            m_region_sizes[i] = size;
            if(m_reuse_buffer) data_size = std::max(size, data_size);
            else data_size += size;
        }
        m_data.resize(data_size);
        for(unsigned i=0; i < data_size; i++) {
            m_data[i] = 'a' + (i%26);
        }
        if(m_preregister_bulk) {
            void* bulk_ptr = m_data.data();
            hg_size_t bulk_size = data_size;
            margo_bulk_create(mid(), 1, &bulk_ptr, &bulk_size, HG_BULK_READ_ONLY, &m_bulk);
        }
    }

    virtual void execute() override {
        auto& _clt = client();
        auto& _tgt = target();
        auto& _ph = ph();
        size_t offset = 0;
        for(unsigned i=0; i < m_num_entries; i++) {
            size_t size = m_region_sizes[i];
            if(m_preregister_bulk) {
                m_region_ids[i] = _clt.create_write_persist(_ph, _tgt, m_bulk, offset, "",  m_region_sizes[i]);
            } else {
                m_region_ids[i] = _clt.create_write_persist(_ph, _tgt, m_data.data()+offset, m_region_sizes[i]);
            }
            if(!m_reuse_buffer) offset += size;
        }
    }

    virtual void teardown() override {
        if(m_erase_on_teardown) {
            auto& _clt = client();
            auto& _tgt = target();
            auto& _ph = ph();
            for(unsigned i=0; i < m_num_entries; i++) {
                _clt.remove(_ph, _tgt, m_region_ids[i]);
            }
        }
        if(m_preregister_bulk) margo_bulk_free(m_bulk);
        m_region_sizes.resize(0); m_region_sizes.shrink_to_fit();
        m_region_ids.resize(0);   m_region_ids.shrink_to_fit();
        m_data.resize(0);         m_data.shrink_to_fit();
    }
};
REGISTER_BENCHMARK("create-write-persist", CreateWritePersistBenchmark);

/**
 * WriteBenchmark executes a series of bake_write
 * operations and measures their duration.
 */
class WriteBenchmark : public AbstractAccessBenchmark {

    protected:

    std::vector<size_t>       m_access_sizes;
    bake::region              m_region_id;
    std::vector<char>         m_data;
    bool                      m_reuse_buffer;
    bool                      m_reuse_region;
    bool                      m_preregister_bulk;
    hg_bulk_t                 m_bulk;

    public:

    template<typename ... T>
    WriteBenchmark(Json::Value& config, T&& ... args)
    : AbstractAccessBenchmark(config, std::forward<T>(args)...) { 
        m_reuse_buffer = getConfigBool(config, "reuse-buffer", false);
        m_reuse_region = getConfigBool(config, "reuse-region", false);
        m_preregister_bulk = getConfigBool(config, "preregister-bulk", false);
    }

    virtual void setup() override {
        // generate region and region sizes
        auto& _clt = client();
        auto& _ph = ph();
        auto& _tgt = target();
        size_t data_size = 0;
        m_access_sizes.resize(m_num_entries);
        size_t region_size = 0;
        for(unsigned i=0; i < m_num_entries; i++) {
            size_t size = m_region_size_range.first + (rand() % (m_region_size_range.second - m_region_size_range.first));
            m_access_sizes[i] = size;
            if(m_reuse_region) region_size = std::max(size, region_size);
            else region_size += size;
            if(m_reuse_buffer) data_size = std::max(size, data_size);
            else data_size += size;
        }
        m_data.resize(data_size);
        for(unsigned i=0; i < data_size; i++) {
            m_data[i] = 'a' + (i%26);
        }
        m_region_id = _clt.create(_ph, _tgt, region_size);
        if(m_preregister_bulk) {
            void* bulk_ptr = m_data.data();
            hg_size_t bulk_size = data_size;
            margo_bulk_create(mid(), 1, &bulk_ptr, &bulk_size, HG_BULK_READ_ONLY, &m_bulk);
        }
    }

    virtual void execute() override {
        auto& _clt = client();
        auto& _tgt = target();
        auto& _ph = ph();
        size_t data_offset = 0;
        size_t region_offset = 0;
        for(unsigned i=0; i < m_num_entries; i++) {
            size_t size = m_access_sizes[i];
            if(m_preregister_bulk) {
                _clt.write(_ph, _tgt, m_region_id, region_offset, m_bulk, data_offset, "",  size);
            } else {
                char* data = m_data.data() + data_offset;
                _clt.write(_ph, _tgt, m_region_id, region_offset, (void*)data, size);
            }
            if(!m_reuse_buffer) data_offset += size;
            if(!m_reuse_region) region_offset += size;
        }
    }

    virtual void teardown() override {
        if(m_erase_on_teardown) {
            auto& _clt = client();
            auto& _tgt = target();
            auto& _ph = ph();
            _clt.remove(_ph, _tgt, m_region_id);
        }
        m_access_sizes.resize(0); m_access_sizes.shrink_to_fit();
        m_data.resize(0);         m_data.shrink_to_fit();
        if(m_preregister_bulk) {
            margo_bulk_free(m_bulk);
        }
    }
};
REGISTER_BENCHMARK("write", WriteBenchmark);

/**
 * ReadBenchmark executes a series of bake_create_write_persist
 * the a series of bake_read operations and measures the duration of the latter.
 */
class ReadBenchmark : public AbstractAccessBenchmark {

    protected:

    std::vector<size_t>       m_access_sizes;
    bake::region              m_region_id;
    std::vector<char>         m_read_data;
    bool                      m_reuse_buffer;
    bool                      m_reuse_region;
    bool                      m_preregister_bulk;
    hg_bulk_t                 m_bulk;

    public:

    template<typename ... T>
    ReadBenchmark(Json::Value& config, T&& ... args)
    : AbstractAccessBenchmark(config, std::forward<T>(args)...) { 
        m_reuse_buffer = getConfigBool(config, "reuse-buffer", false);
        m_reuse_region = getConfigBool(config, "reuse-region", false);
        m_preregister_bulk = getConfigBool(config, "preregister-bulk", false);
    }

    virtual void setup() override {
        auto& _clt = client();
        auto& _tgt = target();
        auto& _ph = ph();
        // generate region sizes
        size_t read_data_size = 0;
        size_t region_size = 0;
        std::vector<char> write_data;
        m_access_sizes.reserve(m_num_entries);
        for(unsigned i=0; i < m_num_entries; i++) {
            size_t size = m_region_size_range.first + (rand() % (m_region_size_range.second - m_region_size_range.first));
            m_access_sizes[i] = size;
            if(m_reuse_buffer) read_data_size = std::max(size, read_data_size);
            else read_data_size += size;
            if(m_reuse_region) region_size = std::max(size, region_size);
            else region_size += size;

        }
        write_data.resize(region_size);
        for(unsigned i=0; i < region_size; i++) {
            write_data[i] = 'a' + (i%26);
        }
        m_region_id = _clt.create_write_persist(_ph, _tgt, (void*)write_data.data(), region_size);
        m_read_data.resize(read_data_size);
        if(m_preregister_bulk) {
            void* bulk_ptr = m_read_data.data();
            hg_size_t bulk_size = read_data_size;
            margo_bulk_create(mid(), 1, &bulk_ptr, &bulk_size, HG_BULK_WRITE_ONLY, &m_bulk);
        }
    }

    virtual void execute() override {
        auto& _clt = client();
        auto& _tgt = target();
        auto& _ph = ph();
        size_t data_offset = 0;
        size_t region_offset = 0;
        for(unsigned i=0; i < m_num_entries; i++) {
            size_t size = m_access_sizes[i];
            if(m_preregister_bulk) {
                _clt.read(_ph, _tgt, m_region_id, region_offset, m_bulk, data_offset, "",  size);
            } else {
                char* data = m_read_data.data() + data_offset;
                _clt.read(_ph, _tgt, m_region_id, region_offset, (void*)data, size);
            }
            if(!m_reuse_buffer) data_offset += size;
            if(!m_reuse_region) region_offset += size;
        }
    }

    virtual void teardown() override {
        if(m_erase_on_teardown) {
            auto& _clt = client();
            auto& _tgt = target();
            auto& _ph = ph();
            _clt.remove(_ph, _tgt, m_region_id);
        }
        m_access_sizes.resize(0); m_access_sizes.shrink_to_fit();
        m_read_data.resize(0);    m_read_data.shrink_to_fit();
        if(m_preregister_bulk) {
            margo_bulk_free(m_bulk);
        }
    }
};
REGISTER_BENCHMARK("read", ReadBenchmark);

/**
 * PersistBenchmark executes a series of bake_persist
 * operations and measures the duration of the latter.
 */
class PersistBenchmark : public AbstractAccessBenchmark {

    protected:

    std::vector<bake::region> m_region_ids;
    std::vector<size_t>       m_access_sizes;

    public:

    template<typename ... T>
    PersistBenchmark(Json::Value& config, T&& ... args)
    : AbstractAccessBenchmark(config, std::forward<T>(args)...) { }

    virtual void setup() override {
        auto& _clt = client();
        auto& _tgt = target();
        auto& _ph = ph();
        m_region_ids.resize(m_num_entries);
        std::vector<char> write_data;
        m_access_sizes.resize(m_num_entries);
        for(unsigned i=0; i < m_num_entries; i++) {
            size_t size = m_region_size_range.first + (rand() % (m_region_size_range.second - m_region_size_range.first));
            m_access_sizes[i] = size;
            if(size > write_data.size()) write_data.resize(size);
        }
        for(unsigned i=0; i < write_data.size(); i++) {
            write_data[i] = 'a' + (i%26);
        }
        for(unsigned i=0; i < m_num_entries; i++) {
            m_region_ids[i] = _clt.create(_ph, _tgt, m_access_sizes[i]);
            _clt.write(_ph, _tgt, m_region_ids[i], 0, (void*)write_data.data(), m_access_sizes[i]);
        }
    }

    virtual void execute() override {
        auto& _clt = client();
        auto& _tgt = target();
        auto& _ph = ph();
        for(unsigned i=0; i < m_num_entries; i++) {
            size_t size = m_access_sizes[i];
            _clt.persist(_ph, _tgt, m_region_ids[i], 0, size);
        }
    }

    virtual void teardown() override {
        if(m_erase_on_teardown) {
            auto& _clt = client();
            auto& _tgt = target();
            auto& _ph = ph();
            for(unsigned i=0; i < m_num_entries; i++) {
                _clt.remove(_ph, _tgt, m_region_ids[i]);
            }
        }
        m_region_ids.resize(0);   m_region_ids.shrink_to_fit();
        m_access_sizes.resize(0); m_access_sizes.shrink_to_fit();
    }
};
REGISTER_BENCHMARK("persist", PersistBenchmark);

static void run_server(MPI_Comm comm, Json::Value& config);
static void run_client(MPI_Comm comm, Json::Value& config);

/**
 * @brief Main function.
 */
int main(int argc, char** argv) {

    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if(argc != 2) {
        if(rank == 0) {
            std::cerr << "Usage: " << argv[0] << " <config.json>" << std::endl;
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
    }

    std::ifstream config_file(argv[1]);
    if(!config_file.good() && rank == 0) {
        std::cerr << "Could not read configuration file " << argv[1] << std::endl;
        MPI_Abort(MPI_COMM_WORLD, -1);
    }

    Json::CharReaderBuilder builder;
    Json::Value config;
    JSONCPP_STRING errs;
    if(!parseFromStream(builder, config_file, &config, &errs)) {
        std::cout << errs << std::endl;
        MPI_Abort(MPI_COMM_WORLD, -1);
        return -1;
    }

    MPI_Comm comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank == 0 ? 0 : 1, rank, &comm);

    if(rank == 0) {
        run_server(comm, config);
    } else {
        run_client(comm, config);
    }

    MPI_Finalize();
    return 0;
}

static void run_server(MPI_Comm comm, Json::Value& config) {
    // initialize Margo
    margo_instance_id mid = MARGO_INSTANCE_NULL;
    std::string protocol = config["protocol"].asString();
    auto& server_config = config["server"];
    bool use_progress_thread = server_config["use-progress-thread"].asBool();
    int  rpc_thread_count = server_config["rpc-thread-count"].asInt();
    auto& provider_config = server_config["provider-config"];
    mid = margo_init(protocol.c_str(), MARGO_SERVER_MODE, use_progress_thread, rpc_thread_count);
    margo_enable_remote_shutdown(mid);
    // serialize server address
    std::vector<char> server_addr_str(256,0);
    hg_size_t buf_size = 256;
    hg_addr_t server_addr = HG_ADDR_NULL;
    margo_addr_self(mid, &server_addr);
    margo_addr_to_string(mid, server_addr_str.data(), &buf_size, server_addr);
    margo_addr_free(mid, server_addr);
    // send server address to client
    MPI_Bcast(&buf_size, sizeof(hg_size_t), MPI_BYTE, 0, MPI_COMM_WORLD);
    MPI_Bcast(server_addr_str.data(), buf_size, MPI_BYTE, 0, MPI_COMM_WORLD);
    // initialize sdskv provider
    auto provider = bake::provider::create(mid);
    // initialize database
    auto& target_config = server_config["target"];
    std::string tgt_path = target_config["path"].asString();
    provider->add_storage_target(tgt_path);
    for(auto it = provider_config.begin(); it != provider_config.end(); it++) {
        std::string key = it.key().asString();
        std::string value = provider_config[key].asString();
        provider->set_config(key.c_str(), value.c_str());
    }
    // notify clients that the database is ready
    MPI_Barrier(MPI_COMM_WORLD);
    // wait for finalize
    margo_wait_for_finalize(mid);
}

static void run_client(MPI_Comm comm, Json::Value& config) {
    // get info from communicator
    int rank, num_clients;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &num_clients);
    Json::StreamWriterBuilder builder;
    const std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    // initialize Margo
    margo_instance_id mid = MARGO_INSTANCE_NULL;
    std::string protocol = config["protocol"].asString();
    mid = margo_init(protocol.c_str(), MARGO_SERVER_MODE, 0, 0);
    // receive server address
    std::vector<char> server_addr_str;
    hg_size_t buf_size;
    hg_addr_t server_addr = HG_ADDR_NULL;
    MPI_Bcast(&buf_size, sizeof(hg_size_t), MPI_BYTE, 0, MPI_COMM_WORLD);
    server_addr_str.resize(buf_size, 0);
    MPI_Bcast(server_addr_str.data(), buf_size, MPI_BYTE, 0, MPI_COMM_WORLD);
    margo_addr_lookup(mid, server_addr_str.data(), &server_addr);
    // wait for server to have initialize the database
    MPI_Barrier(MPI_COMM_WORLD);
    {
        // open remote database
        bake::client client(mid);
        bake::provider_handle ph(client, server_addr);
        std::vector<bake::target> targets = client.probe(ph);
        bake::target target = targets[0];
        // initialize the RNG seed
        int seed = config["seed"].asInt();
        // initialize benchmark instances
        std::vector<std::unique_ptr<AbstractBenchmark>> benchmarks;
        std::vector<unsigned> repetitions;
        std::vector<std::string> types;
        benchmarks.reserve(config["benchmarks"].size());
        repetitions.reserve(config["benchmarks"].size());
        types.reserve(config["benchmarks"].size());
        for(auto& bench_config : config["benchmarks"]) {
            std::string type = bench_config["type"].asString();
            types.push_back(type);
            benchmarks.push_back(AbstractBenchmark::create(type, bench_config, comm, mid, client, ph, target));
            repetitions.push_back(bench_config["repetitions"].asUInt());
        }
        // main execution loop
        for(unsigned i = 0; i < benchmarks.size(); i++) {
            auto& bench  = benchmarks[i];
            unsigned rep = repetitions[i];
            // reset the RNG
            srand(seed + rank*1789);
            std::vector<double> local_timings(rep);
            for(unsigned j = 0; j < rep; j++) {
                MPI_Barrier(comm);
                // benchmark setup
                bench->setup();
                MPI_Barrier(comm);
                // benchmark execution
                double t_start = MPI_Wtime();
                bench->execute();
                double t_end = MPI_Wtime();
                local_timings[j] = t_end - t_start;
                MPI_Barrier(comm);
                // teardown
                bench->teardown();
            }
            // exchange timings
            std::vector<double> global_timings(rep*num_clients);
            if(num_clients != 1) {
                MPI_Gather(local_timings.data(), local_timings.size(), MPI_DOUBLE,
                       global_timings.data(), local_timings.size(), MPI_DOUBLE, 0, comm);
            } else {
                std::copy(local_timings.begin(), local_timings.end(), global_timings.begin());
            }
            // print report
            if(rank == 0) {
                size_t n = global_timings.size();
                std::cout << "================ " << types[i] << " ================" << std::endl;
                writer->write(config["benchmarks"][i], &std::cout);
                std::cout << std::endl;
                std::cout << "-----------------" << std::string(types[i].size(),'-') << "-----------------" << std::endl;
                double average  = std::accumulate(global_timings.begin(), global_timings.end(), 0.0) / n;
                double variance = std::accumulate(global_timings.begin(), global_timings.end(), 0.0, [average](double acc, double x) {
                        return acc + std::pow((x - average),2);
                    });
                variance /= n;
                double stddev = std::sqrt(variance);
                std::sort(global_timings.begin(), global_timings.end());
                double min = global_timings[0];
                double max = global_timings[global_timings.size()-1];
                double median = (n % 2) ? global_timings[n/2] : ((global_timings[n/2] + global_timings[n/2 + 1])/2.0);
                double q1 = global_timings[n/4];
                double q3 = global_timings[(3*n)/4];
                std::cout << std::setprecision(9) << std::fixed;
                std::cout << "Samples         : " << n << std::endl;
                std::cout << "Average(sec)    : " << average << std::endl;
                std::cout << "Variance(sec^2) : " << variance << std::endl;
                std::cout << "StdDev(sec)     : " << stddev << std::endl;
                std::cout << "Minimum(sec)    : " << min << std::endl;
                std::cout << "Q1(sec)         : " << q1 << std::endl;
                std::cout << "Median(sec)     : " << median << std::endl;
                std::cout << "Q3(sec)         : " << q3 << std::endl;
                std::cout << "Maximum(sec)    : " << max << std::endl;
            }
        }
        // wait for all the clients to be done with their tasks
        MPI_Barrier(comm);
        // shutdown server and finalize margo
        if(rank == 0)
            margo_shutdown_remote_instance(mid, server_addr);
    }
    margo_addr_free(mid, server_addr);
    margo_finalize(mid);
}
