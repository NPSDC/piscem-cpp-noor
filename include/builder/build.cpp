#include "../util.hpp"
#include "../gz/zip_stream.cpp"
#include "../util.hpp"

#include "../dictionary.hpp"
#include "../buckets.hpp"
#include "../util.hpp"
#include "build_util_types.hpp"

#include <numeric>  // for std::partial_sum
#include <vector>

#include "../../external/pthash/include/pthash.hpp"
#include "../../external/pthash/external/essentials/include/essentials.hpp"
#include "../print_info.cpp"

namespace sshash {

struct parse_data {
    parse_data() : num_kmers(0) {}
    uint64_t num_kmers;
    minimizers_tuples minimizers;
    compact_string_pool strings;
};

void parse_file(std::istream& is, parse_data& data, build_configuration const& build_config) {
    uint64_t k = build_config.k;
    uint64_t m = build_config.m;
    uint64_t seed = build_config.seed;
    uint64_t max_num_kmers = build_config.max_num_kmers;
    uint64_t max_num_kmers_in_string = k - m + 1;
    uint64_t block_size = 2 * k - m;  // max_num_kmers_in_string + k - 1

    if (max_num_kmers_in_string >= (1ULL << (sizeof(num_kmers_in_string_uint_type) * 8))) {
        throw std::runtime_error("max_num_kmers_in_string " +
                                 std::to_string(max_num_kmers_in_string) + " does not fit into " +
                                 std::to_string(sizeof(num_kmers_in_string_uint_type) * 8) +
                                 " bits");
    }

    /* fit into the wanted number of bits */
    assert(max_num_kmers_in_string < (1ULL << (sizeof(num_kmers_in_string_uint_type) * 8)));

    compact_string_pool::builder builder(k);

    std::string line;
    uint64_t prev_minimizer = constants::invalid;

    uint64_t begin = 0;           // begin of parsed string in line
    uint64_t end = 0;             // end of parsed string in line
    uint64_t num_read_lines = 0;  // total read lines
    uint64_t num_read_bases = 0;
    bool glue = false;

    auto append_string = [&]() {
        if (line.empty() or prev_minimizer == constants::invalid or begin == end) return false;

        assert(end > begin);
        char const* string = line.data() + begin;
        uint64_t size = (end - begin) + k - 1;
        assert(util::is_valid(string, size));

        /* if num_kmers_in_string > k - m + 1, then split the string into blocks */
        uint64_t num_kmers_in_string = end - begin;
        uint64_t num_blocks = num_kmers_in_string / max_num_kmers_in_string +
                              (num_kmers_in_string % max_num_kmers_in_string != 0);
        assert(num_blocks > 0);
        for (uint64_t i = 0; i != num_blocks; ++i) {
            uint64_t n = block_size;
            if (i == num_blocks - 1) n = size;
            uint64_t num_kmers_in_block = n - k + 1;
            assert(num_kmers_in_block <= max_num_kmers_in_string);
            data.minimizers.emplace_back(prev_minimizer, builder.offset, num_kmers_in_block);
            builder.append(string + i * max_num_kmers_in_string, n, glue);
            if (glue) {
                assert(data.minimizers.back().offset > k - 1);
                data.minimizers.back().offset -= k - 1;
            }
            size -= max_num_kmers_in_string;
        }

        return true;
    };

    while (!is.eof() and data.num_kmers != max_num_kmers) {
        std::getline(is, line);  // skip header line
        std::getline(is, line);

        if (line.size() < k) continue;

        if (++num_read_lines % 100000 == 0) {
            std::cout << "read " << num_read_lines << " lines, " << num_read_bases << " bases, "
                      << data.num_kmers << " kmers" << std::endl;
        }

        begin = 0;
        end = 0;
        glue = false;
        prev_minimizer = constants::invalid;
        num_read_bases += line.size();

        while (end != line.size() - k + 1) {
            char const* kmer = line.data() + end;
            assert(util::is_valid(kmer, k));
            uint64_t uint64_kmer = util::string_to_uint64_no_reverse(kmer, k);
            uint64_t minimizer = util::compute_minimizer(uint64_kmer, k, m, seed);

            if (build_config.canonical_parsing) {
                uint64_t uint64_kmer_rc = util::compute_reverse_complement(uint64_kmer, k);
                uint64_t minimizer_rc = util::compute_minimizer(uint64_kmer_rc, k, m, seed);
                minimizer = std::min<uint64_t>(minimizer, minimizer_rc);
            }

            if (prev_minimizer == constants::invalid) prev_minimizer = minimizer;
            if (minimizer != prev_minimizer) {
                append_string();
                begin = end;
                prev_minimizer = minimizer;
                glue = true;
            }

            ++data.num_kmers;
            if (data.num_kmers == max_num_kmers) break;

            ++end;
        }

        append_string();
    }

    builder.finalize();
    builder.build(data.strings);

    std::cout << "read " << num_read_lines << " lines, " << num_read_bases << " bases, "
              << data.num_kmers << " kmers" << std::endl;
    std::cout << "num_kmers " << data.num_kmers << std::endl;
    std::cout << "num_strings " << data.strings.size() << std::endl;
    std::cout << "num_pieces " << data.strings.pieces.size() << " (+"
              << (2.0 * data.strings.pieces.size() * (k - 1)) / data.num_kmers << " [bits/kmer])"
              << std::endl;
}

parse_data parse_file(std::string const& filename, build_configuration const& build_config) {
    std::ifstream is(filename.c_str());
    if (!is.good()) throw std::runtime_error("error in opening the file '" + filename + "'");
    std::cout << "reading file '" << filename << "'..." << std::endl;
    parse_data data;
    if (util::ends_with(filename, ".gz")) {
        zip_istream zis(is);
        parse_file(zis, data, build_config);
    } else {
        parse_file(is, data, build_config);
    }
    is.close();
    return data;
}

buckets_statistics build_index(parse_data& data, minimizers const& m_minimizers,
                               buckets& m_buckets) {
    uint64_t num_buckets = m_minimizers.size();
    uint64_t num_kmers = data.num_kmers;
    uint64_t num_strings = data.strings.size();
    std::vector<uint64_t> num_strings_before_bucket(num_buckets + 1, -1);
    num_strings_before_bucket[0] = 0;
    pthash::compact_vector::builder offsets;
    offsets.resize(num_strings, std::ceil(std::log2(data.strings.num_bits() / 2)));

    std::cout << "num_symbols_in_string " << data.strings.num_bits() / 2 << std::endl;
    std::cout << "bits_per_offset = ceil(log2(" << data.strings.num_bits() / 2
              << ")) = " << std::ceil(std::log2(data.strings.num_bits() / 2)) << std::endl;

    for (auto it = data.minimizers.begin(); it.has_next(); it.next()) {
        uint64_t bucket_id = m_minimizers.lookup(it.minimizer());
        num_strings_before_bucket[bucket_id + 1] += it.list().size();
    }
    std::partial_sum(num_strings_before_bucket.begin(), num_strings_before_bucket.end(),
                     num_strings_before_bucket.begin());

    buckets_statistics buckets_stats(num_buckets, num_kmers, num_strings);

    uint64_t num_singletons = 0;
    for (auto it = data.minimizers.begin(); it.has_next(); it.next()) {
        uint64_t bucket_id = m_minimizers.lookup(it.minimizer());
        uint64_t base = num_strings_before_bucket[bucket_id] + bucket_id;
        uint64_t num_strings_in_bucket =
            (num_strings_before_bucket[bucket_id + 1] + bucket_id + 1) - base;
        assert(num_strings_in_bucket > 0);
        if (num_strings_in_bucket == 1) ++num_singletons;
        buckets_stats.add_num_strings_in_bucket(num_strings_in_bucket);
        uint64_t offset_pos = 0;
        auto list = it.list();
        for (auto [offset, num_kmers_in_string] : list) {
            offsets.set(base + offset_pos++, offset);
            buckets_stats.add_num_kmers_in_string(num_strings_in_bucket, num_kmers_in_string);
        }
        assert(offset_pos == num_strings_in_bucket);
    }

    std::cout << "num_singletons " << num_singletons << "/" << num_buckets << " ("
              << (num_singletons * 100.0) / num_buckets << "%)" << std::endl;

    m_buckets.pieces.encode(data.strings.pieces.begin(), data.strings.pieces.size());
    m_buckets.num_strings_before_bucket.encode(num_strings_before_bucket.begin(),
                                               num_strings_before_bucket.size());
    offsets.build(m_buckets.offsets);
    m_buckets.strings.swap(data.strings.strings);

    return buckets_stats;
}

struct empty_bucket_runtime_error : public std::runtime_error {
    empty_bucket_runtime_error()
        : std::runtime_error("try a different choice of l or change seed") {}
};

void build_skew_index(skew_index& m_skew_index, parse_data& data, buckets const& m_buckets,
                      build_configuration const& build_config,
                      buckets_statistics const& buckets_stats) {
    uint64_t min_log2_size = m_skew_index.min_log2;
    uint64_t max_log2_size = m_skew_index.max_log2;

    m_skew_index.max_num_strings_in_bucket = buckets_stats.max_num_strings_in_bucket();
    m_skew_index.log2_max_num_strings_in_bucket =
        std::ceil(std::log2(buckets_stats.max_num_strings_in_bucket()));

    std::cout << "max_num_strings_in_bucket " << m_skew_index.max_num_strings_in_bucket
              << std::endl;
    std::cout << "log2_max_num_strings_in_bucket " << m_skew_index.log2_max_num_strings_in_bucket
              << std::endl;

    uint64_t num_buckets_in_skew_index = 0;
    for (auto it = data.minimizers.begin(); it.has_next(); it.next()) {
        if (it.list().size() > (1ULL << min_log2_size)) ++num_buckets_in_skew_index;
    }
    std::cout << "num_buckets_in_skew_index " << num_buckets_in_skew_index << "/"
              << buckets_stats.num_buckets() << "("
              << (num_buckets_in_skew_index * 100.0) / buckets_stats.num_buckets() << "%)"
              << std::endl;

    if (num_buckets_in_skew_index == 0) return;

    std::vector<list_type> lists;
    lists.reserve(num_buckets_in_skew_index);
    for (auto it = data.minimizers.begin(); it.has_next(); it.next()) {
        auto list = it.list();
        if (list.size() > (1ULL << min_log2_size)) lists.push_back(list);
    }
    assert(lists.size() == num_buckets_in_skew_index);
    std::sort(lists.begin(), lists.end(),
              [](list_type const& x, list_type const& y) { return x.size() < y.size(); });

    uint64_t num_partitions = max_log2_size - min_log2_size + 1;
    if (buckets_stats.max_num_strings_in_bucket() < (1ULL << max_log2_size)) {
        num_partitions = m_skew_index.log2_max_num_strings_in_bucket - min_log2_size;
    }
    std::cout << "num_partitions " << num_partitions << std::endl;

    std::vector<uint64_t> num_kmers_in_partition(num_partitions, 0);
    m_skew_index.mphfs.resize(num_partitions);
    m_skew_index.positions.resize(num_partitions);

    {
        std::cout << "computing partitions..." << std::endl;

        uint64_t partition_id = 0;
        uint64_t lower = 1ULL << min_log2_size;
        uint64_t upper = 2 * lower;
        uint64_t num_kmers_in_skew_index = 0;
        for (uint64_t i = 0; i != lists.size() + 1; ++i) {
            if (i == lists.size() or lists[i].size() > upper) {
                std::cout << "num_kmers belonging to buckets of size > " << lower
                          << " and <= " << upper << ": " << num_kmers_in_partition[partition_id]
                          << std::endl;
                if (num_kmers_in_partition[partition_id] == 0) {
                    std::cout << "==> Empty bucket detected:\n";
                    std::cout << "there is no k-mer that belongs to a list of size > " << lower
                              << " and <= " << upper << std::endl;
                    throw empty_bucket_runtime_error();
                }
                util::check_hash_collision_probability(num_kmers_in_partition[partition_id]);
                num_kmers_in_skew_index += num_kmers_in_partition[partition_id];
                partition_id += 1;

                if (i == lists.size()) break;

                lower = upper;
                upper = 2 * lower;
                if (partition_id == num_partitions - 1) {
                    upper = m_skew_index.max_num_strings_in_bucket;
                }

                /*
                    If still larger than upper, then there is an empty bucket
                    and we should try different parameters.
                */
                if (lists[i].size() > upper) {
                    std::cout << "==> Empty bucket detected:\n";
                    std::cout << "there is no list of size > " << lower << " and <= " << upper
                              << std::endl;
                    throw empty_bucket_runtime_error();
                }
            }

            assert(lists[i].size() > lower and lists[i].size() <= upper);
            for (auto [offset, num_kmers_in_string] : lists[i]) {
                (void)offset;  // unused
                num_kmers_in_partition[partition_id] += num_kmers_in_string;
            }
        }
        assert(partition_id == num_partitions);
        std::cout << "num_kmers_in_skew_index " << num_kmers_in_skew_index << "("
                  << (num_kmers_in_skew_index * 100.0) / buckets_stats.num_kmers() << "%)"
                  << std::endl;
        assert(num_kmers_in_skew_index == std::accumulate(num_kmers_in_partition.begin(),
                                                          num_kmers_in_partition.end(),
                                                          uint64_t(0)));
    }

    {
        pthash::build_configuration mphf_config;
        mphf_config.c = build_config.c;
        mphf_config.alpha = 0.94;
        mphf_config.seed = 1234567890;  // my favourite seed
        mphf_config.minimal_output = true;
        mphf_config.verbose_output = false;
        mphf_config.num_threads = std::thread::hardware_concurrency() >= 8 ? 8 : 1;

        std::cout << "building PTHash mphfs with " << mphf_config.num_threads << " threads..."
                  << std::endl;

        uint64_t partition_id = 0;
        uint64_t lower = 1ULL << min_log2_size;
        uint64_t upper = 2 * lower;

        std::vector<uint64_t> keys_in_partition;
        keys_in_partition.reserve(num_kmers_in_partition.front());

        for (uint64_t i = 0; i != lists.size() + 1; ++i) {
            if (i == lists.size() or lists[i].size() > upper) {
                auto& mphf = m_skew_index.mphfs[partition_id];
                mphf.build_in_internal_memory(keys_in_partition.begin(), keys_in_partition.size(),
                                              mphf_config);

                std::cout << "lower " << lower << "; upper " << upper << "; ";
                std::cout << "built mphs[" << partition_id << "] for " << keys_in_partition.size()
                          << " keys; ";
                std::cout << "bits/key = " << static_cast<double>(mphf.num_bits()) / mphf.num_keys()
                          << std::endl;

                partition_id += 1;

                if (i == lists.size()) break;

                lower = upper;
                upper = 2 * lower;
                if (partition_id == num_partitions - 1) {
                    upper = m_skew_index.max_num_strings_in_bucket;
                }
                keys_in_partition.clear();
            }

            assert(lists[i].size() > lower and lists[i].size() <= upper);
            for (auto [offset, num_kmers_in_string] : lists[i]) {
                bit_vector_iterator bv_it(m_buckets.strings, 2 * offset);
                for (uint64_t i = 0; i != num_kmers_in_string; ++i) {
                    uint64_t read_kmer = bv_it.read(2 * build_config.k);
                    keys_in_partition.push_back(read_kmer);
                    bv_it.eat(2);
                }
            }
        }
        assert(partition_id == num_partitions);
    }

    {
        std::cout << "building positions..." << std::endl;

        pthash::compact_vector::builder cvb_positions;
        uint64_t lower = 1ULL << min_log2_size;
        uint64_t upper = 2 * lower;
        uint64_t num_bits_per_pos = min_log2_size + 1;
        uint64_t partition_id = 0;

        cvb_positions.resize(num_kmers_in_partition[partition_id], num_bits_per_pos);
        auto const* mphf = &(m_skew_index.mphfs[partition_id]);

        for (uint64_t i = 0; i != lists.size() + 1; ++i) {
            if (i == lists.size() or lists[i].size() > upper) {
                auto& positions = m_skew_index.positions[partition_id];
                cvb_positions.build(positions);

                std::cout << "lower " << lower << "; upper " << upper << "; num_bits_per_pos "
                          << num_bits_per_pos << "; ";
                std::cout << "built positions[" << partition_id << "] for " << positions.size()
                          << " keys; ";
                std::cout << "bits/key = " << (positions.bytes() * 8.0) / positions.size()
                          << std::endl;

                partition_id += 1;

                if (i == lists.size()) break;

                lower = upper;
                upper = 2 * lower;
                num_bits_per_pos += 1;
                if (partition_id == num_partitions - 1) {
                    upper = m_skew_index.max_num_strings_in_bucket;
                    num_bits_per_pos = m_skew_index.log2_max_num_strings_in_bucket;
                }

                cvb_positions.resize(num_kmers_in_partition[partition_id], num_bits_per_pos);
                mphf = &(m_skew_index.mphfs[partition_id]);
            }

            assert(lists[i].size() > lower and lists[i].size() <= upper);
            uint64_t list_id = 0;
            for (auto [offset, num_kmers_in_string] : lists[i]) {
                bit_vector_iterator bv_it(m_buckets.strings, 2 * offset);
                for (uint64_t i = 0; i != num_kmers_in_string; ++i) {
                    uint64_t read_kmer = bv_it.read(2 * build_config.k);
                    uint64_t pos = mphf->operator()(read_kmer);
                    assert(pos < cvb_positions.size());
                    assert(list_id < (1ULL << cvb_positions.width()));
                    cvb_positions.set(pos, list_id);
                    bv_it.eat(2);
                }
                ++list_id;
            }
        }
        assert(partition_id == num_partitions);
    }

    std::cout << "num_bits_for_skew_index " << m_skew_index.num_bits() << "("
              << static_cast<double>(m_skew_index.num_bits()) / buckets_stats.num_kmers()
              << " [bits/kmer])" << std::endl;
}

void print_time(double time, uint64_t num_kmers, std::string const& message) {
    std::cout << "=== " << message << " " << time / 1000000 << " [sec] ("
              << (time * 1000) / num_kmers << " [ns/kmer])" << std::endl;
}

void dictionary::build(std::string const& filename, build_configuration const& build_config) {
    /* Validate build configuration */
    if (build_config.k == 0) throw std::runtime_error("k must be > 0");
    if (build_config.m == 0) throw std::runtime_error("m must be > 0");
    if (build_config.m > build_config.k) throw std::runtime_error("m must be <= k");
    if (build_config.l > constants::max_l) {
        throw std::runtime_error("l must be <= " + std::to_string(constants::max_l));
    }

    m_k = build_config.k;
    m_m = build_config.m;
    m_seed = build_config.seed;
    m_canonical_parsing = build_config.canonical_parsing;
    m_skew_index.min_log2 = build_config.l;

    std::vector<double> timings;
    essentials::timer_type timer;

    // step 1: parse the input file and build compact string pool
    timer.start();
    auto data = parse_file(filename, build_config);
    m_size = data.num_kmers;
    timer.stop();
    timings.push_back(timer.elapsed());
    print_time(timings.back(), data.num_kmers, "parse_file");
    timer.reset();

    // step 2: sort minimizers and build MPHF
    timer.start();
    data.minimizers.sort();
    uint64_t num_buckets = 0;
    for (auto it = data.minimizers.begin(); it.has_next(); it.next()) ++num_buckets;
    m_minimizers.build(data.minimizers.begin(), num_buckets);
    timer.stop();
    timings.push_back(timer.elapsed());
    print_time(timings.back(), data.num_kmers, "build_minimizers");
    timer.reset();

    // step 3: build index
    timer.start();
    auto buckets_stats = build_index(data, m_minimizers, m_buckets);
    timer.stop();
    timings.push_back(timer.elapsed());
    print_time(timings.back(), data.num_kmers, "build_index");
    timer.reset();

    // step 4: build skew index
    timer.start();
    build_skew_index(m_skew_index, data, m_buckets, build_config, buckets_stats);
    timer.stop();
    timings.push_back(timer.elapsed());
    print_time(timings.back(), data.num_kmers, "build_skew_index");
    timer.reset();

    double total_time = std::accumulate(timings.begin(), timings.end(), 0.0);
    print_time(total_time, data.num_kmers, "total_time");

    print_space_breakdown();

    if (build_config.verbose) buckets_stats.print();
}

}  // namespace sshash