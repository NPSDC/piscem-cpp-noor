#pragma once

#include "common.hpp"

namespace sshash {

bool check_correctness_lookup_access(std::istream& is, dictionary const& dict) {
    uint64_t k = dict.k();
    uint64_t n = dict.size();

    std::string line;
    uint64_t pos = 0;
    uint64_t num_kmers = 0;

    std::string got_kmer_str(k, 0);
    std::string expected_kmer_str(k, 0);

    std::cout << "checking correctness of access and positive lookup..." << std::endl;
    pthash::bit_vector_builder got(n, 0);
    __uint128_t sum = 0;

    while (appendline(is, line)) {
        if (line.size() == pos || line[pos] == '>' || line[pos] == ';') {
            // comment or empty line restart the term buffer
            line.clear();
            continue;
        }
        for (uint64_t i = 0; i + k <= line.size(); ++i) {
            assert(util::is_valid(line.data() + i, k));
            uint64_t uint64_kmer = util::string_to_uint64_no_reverse(line.data() + i, k);

            if (num_kmers != 0 and num_kmers % 5000000 == 0) {
                std::cout << "checked " << num_kmers << " kmers" << std::endl;
            }
            if ((num_kmers & 1) == 0) {
                /* transform 50% of the kmers into their reverse complements */
                uint64_kmer = util::compute_reverse_complement(uint64_kmer, k);
            }
            util::uint64_to_string_no_reverse(uint64_kmer, expected_kmer_str.data(), k);
            uint64_t id = dict.lookup(expected_kmer_str.c_str());
            if (id == constants::invalid) {
                std::cout << "kmer '" << expected_kmer_str << "' not found!" << std::endl;
            }
            assert(id != constants::invalid);
            if (id >= n) {
                std::cout << "ERROR: id out of range " << id << "/" << n << std::endl;
                return false;
            }

            if (got.get(id) == true) {
                std::cout << "id " << id << " was already assigned!" << std::endl;
                return false;
            }

            got.set(id);
            sum += id;

            // check access
            dict.access(id, got_kmer_str.data());
            uint64_t got_uint64_kmer = util::string_to_uint64_no_reverse(got_kmer_str.data(), k);
            uint64_t got_uint64_kmer_rc = util::compute_reverse_complement(got_uint64_kmer, k);
            if (got_uint64_kmer != uint64_kmer and got_uint64_kmer_rc != uint64_kmer) {
                std::cout << "ERROR: got '" << got_kmer_str << "' but expected '"
                          << expected_kmer_str << "'" << std::endl;
            }

            ++num_kmers;
        }
        if (line.size() > k - 1) {
            std::copy(line.data() + line.size() - (k - 1), line.data() + line.size(), line.data());
            line.resize(k - 1);
            pos = line.size();
        } else {
            pos = 0;
        }
    }
    std::cout << "checked " << num_kmers << " kmers" << std::endl;

    if (n != num_kmers) {
        std::cout << "expected " << n << " kmers but checked " << num_kmers << std::endl;
        return false;
    }

    if (sum != (n * (n - 1)) / 2) {
        std::cout << "ERROR: index contains duplicates" << std::endl;
        return false;
    }

    std::cout << "EVERYTHING OK!" << std::endl;

    std::cout << "checking correctness of negative lookup with random kmers..." << std::endl;
    uint64_t num_lookups = std::min<uint64_t>(1000000, n);
    for (uint64_t i = 0; i != num_lookups; ++i) {
        random_kmer(got_kmer_str.data(), k);
        /*
            We could use a std::unordered_set to check if kmer is really absent,
            but that would take much more memory...
        */
        uint64_t id = dict.lookup(got_kmer_str.c_str());
        if (id != constants::invalid) {
            std::cout << "kmer '" << got_kmer_str << "' found!" << std::endl;
        }
    }

    std::cout << "EVERYTHING OK!" << std::endl;
    return true;
}

/*
   The input file must be the one the index was built from.
   Throughout the code, we assume the input does not contain any duplicate.
*/
bool check_correctness_lookup_access(dictionary const& dict, std::string const& filename) {
    std::ifstream is(filename.c_str());
    if (!is.good()) throw std::runtime_error("error in opening the file '" + filename + "'");
    bool good = true;
    if (util::ends_with(filename, ".gz")) {
        zip_istream zis(is);
        good = check_correctness_lookup_access(zis, dict);
    } else {
        good = check_correctness_lookup_access(is, dict);
    }
    is.close();
    return good;
}

bool check_dictionary(dictionary const& dict) {
    uint64_t k = dict.k();
    uint64_t n = dict.size();
    std::cout << "checking correctness of access and positive lookup..." << std::endl;
    uint64_t id = 0;
    std::string kmer(k, 0);
    for (; id != n; ++id) {
        if (id != 0 and id % 5000000 == 0) std::cout << "checked " << id << " kmers" << std::endl;
        dict.access(id, kmer.data());
        uint64_t got_id = dict.lookup(kmer.c_str());
        if (got_id == constants::invalid) {
            std::cout << "kmer '" << kmer << "' not found!" << std::endl;
            return false;
        }
        if (got_id >= n) {
            std::cout << "ERROR: id out of range " << got_id << "/" << n << std::endl;
            return false;
        }
        if (got_id != id) {
            std::cout << "expected id " << id << " but got id " << got_id << std::endl;
            return false;
        }
    }
    std::cout << "checked " << id << " kmers" << std::endl;
    std::cout << "EVERYTHING OK!" << std::endl;
    return true;
}

bool check_correctness_iterator(dictionary const& dict) {
    std::cout << "checking correctness of iterator..." << std::endl;
    std::string expected_kmer(dict.k(), 0);
    constexpr uint64_t runs = 3;
    essentials::uniform_int_rng<uint64_t> distr(0, dict.size() - 1, essentials::get_random_seed());
    for (uint64_t run = 0; run != runs; ++run) {
        uint64_t from_kmer_id = distr.gen();
        auto it = dict.at(from_kmer_id);
        while (it.has_next()) {
            auto [kmer_id, kmer] = it.next();
            dict.access(kmer_id, expected_kmer.data());
            if (kmer != expected_kmer or kmer_id != from_kmer_id) {
                std::cout << "got (" << kmer_id << ",'" << kmer << "')";
                std::cout << " but ";
                std::cout << "expected (" << from_kmer_id << ",'" << expected_kmer << "')"
                          << std::endl;
                return false;
            }
            ++from_kmer_id;
        }
        assert(from_kmer_id == dict.size());
    }
    std::cout << "EVERYTHING OK!" << std::endl;
    return true;
}

}  // namespace sshash