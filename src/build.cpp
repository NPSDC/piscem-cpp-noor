#include <iostream>

#include "../external/pthash/external/cmd_line_parser/include/parser.hpp"
#include "../include/dictionary.hpp"
#include "../include/lookup.cpp"
#include "../include/info.cpp"
#include "../include/builder/build.cpp"
#include "build_contig_table.cpp"
#include "bench_utils.hpp"
#include "check_utils.hpp"

using namespace sshash;

int main(int argc, char** argv) {
    cmd_line_parser::parser parser(argc, argv);

    /* mandatory arguments */
    parser.add("input_files_basename",
               "Must be the basename of input cuttlefish files (expected suffixes are .cf_seq and "
               ".cf_seg, possibly ending with '.gz'.)");
    parser.add("k", "K-mer length (must be <= " + std::to_string(constants::max_k) + ").");
    parser.add("m", "Minimizer length (must be < k).");

    /* optional arguments */
    parser.add("seed",
               "Seed for construction (default is " + std::to_string(constants::seed) + ").", "-s",
               false);
    parser.add("l",
               "A (integer) constant that controls the space/time trade-off of the dictionary. "
               "A reasonable values lies between 2 and 12 (default is " +
                   std::to_string(constants::min_l) + ").",
               "-l", false);
    parser.add("c",
               "A (floating point) constant that trades construction speed for space effectiveness "
               "of minimal perfect hashing. "
               "A reasonable value lies between 3.0 and 10.0 (default is " +
                   std::to_string(constants::c) + ").",
               "-c", false);
    parser.add("canonical_parsing",
               "Canonical parsing of k-mers. This option changes the parsing and results in a "
               "trade-off between index space and lookup time.",
               "--canonical-parsing", true);
    parser.add("store_abundances", "Also store the abundances in compressed format.",
               "--abundances", true);
    parser.add("output_filename", "Output file name where the data structure will be serialized.",
               "-o", false);
    parser.add("check", "Check correctness after construction.", "--check", true);
    parser.add("bench", "Run benchmark after construction.", "--bench", true);
    parser.add("verbose", "Verbose output during construction.", "--verbose", true);

    if (!parser.parse()) return 1;

    auto input_files_basename = parser.get<std::string>("input_files_basename");
    auto k = parser.get<uint64_t>("k");
    auto m = parser.get<uint64_t>("m");

    build_configuration build_config;
    build_config.k = k;
    build_config.m = m;

    if (parser.parsed("seed")) build_config.seed = parser.get<uint64_t>("seed");
    if (parser.parsed("l")) build_config.l = parser.get<double>("l");
    if (parser.parsed("c")) build_config.c = parser.get<double>("c");
    build_config.canonical_parsing = parser.get<bool>("canonical_parsing");
    build_config.store_abundances = parser.get<bool>("store_abundances");
    build_config.verbose = parser.get<bool>("verbose");
    build_config.print();

    if (!parser.parsed("output_filename")) {
        essentials::logger("output filename is required but missing!\n");
        return 1;
    }
    auto output_filename = parser.get<std::string>("output_filename");

    {
        // make this scope here and put dict inside of it to
        // ensure it goes out of scope before we build the
        // contig table
        auto input_seq = input_files_basename + ".cf_seg";
        dictionary dict;
        dict.build(input_seq, build_config);
        assert(dict.k() == k);
        auto output_seqidx = output_filename + ".sshash";
        essentials::logger("saving data structure to disk...");
        essentials::save(dict, output_seqidx.c_str());
        essentials::logger("DONE");
    }

    // now build the contig table
    return build_contig_table_main(input_files_basename, k, output_filename);
}
