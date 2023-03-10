#ifndef __SC_UTIL_HPP__
#define __SC_UTIL_HPP__

#include <memory>
#include "../peglib.h"
#include "../itlib/small_vector.hpp"

// forward declaration, even though this
// is in the *same file*
class custom_protocol;

namespace single_cell {

namespace util {

enum class BarCodeRecovered : uint8_t { OK, RECOVERED, NOT_RECOVERED };

BarCodeRecovered recover_barcode(std::string& sequence) {
    size_t pos = sequence.find_first_not_of("ACTGactg");
    if (pos == std::string::npos) { return BarCodeRecovered::OK; }

    // Randomly assigning 'A' to first base with 'N'
    sequence[pos] = 'A';
    size_t invalid_pos = sequence.find_first_not_of("ACTGactg", pos);
    return (invalid_pos == std::string::npos) ? BarCodeRecovered::RECOVERED
                                              : BarCodeRecovered::NOT_RECOVERED;
}

enum class geo_tag_type { BC, UMI, READ, FIXED, DISCARD };

struct geo_part {
    geo_tag_type ttype{geo_tag_type::DISCARD};
    int64_t len{-1};
    friend std::ostream& operator<<(std::ostream& os, const geo_part& gp);
};

std::ostream& operator<<(std::ostream& os, const geo_part& gp) {
    switch (gp.ttype) {
        case geo_tag_type::BC: {
            os << "BC [" << gp.len << "]";
            break;
        }
        case geo_tag_type::UMI: {
            os << "UMI [" << gp.len << "]";
            break;
        }
        case geo_tag_type::READ: {
            os << "R [" << gp.len << "]";
            break;
        }
        case geo_tag_type::FIXED: {
            os << "F [" << gp.len << "]";
            break;
        }
        case geo_tag_type::DISCARD: {
            os << "X [" << gp.len << "]";
            break;
        }
    }
    return os;
}

struct protocol_state {
    geo_tag_type curr_geo_type{geo_tag_type::DISCARD};
    std::vector<geo_part> geo_parts;
    std::vector<geo_part> geo_parts_r1;
    std::vector<geo_part> geo_parts_r2;
};

std::unique_ptr<custom_protocol> parse_custom_geometry(std::string& geom) {
  /*
   // definition using parser combinators (seemingly *not* faster to compile)
  using namespace peg;
  Definition SPECIFICATION, R1_DESC, R2_DESC, BOUNDED_DESC, UNBOUNDED_DESC, BARCODE, UMI, DISCARD, FIXED, READ, SEQUENCE, LENGTHS, LENGTH;
  SPECIFICATION <= seq(R1_DESC, R2_DESC);
  R1_DESC <= seq(liti("1{"), cho( UNBOUNDED_DESC, seq(rep(BOUNDED_DESC, 1, 10), rep(UNBOUNDED_DESC, 0, 1)) ), liti("}"));
  R2_DESC <= seq(liti("2{"), cho( UNBOUNDED_DESC, seq(rep(BOUNDED_DESC, 1, 10), rep(UNBOUNDED_DESC, 0, 1)) ), liti("}"));
  BOUNDED_DESC <= cho( seq(BARCODE, chr('['), LENGTHS, chr(']')), seq(UMI, chr('['), LENGTHS, chr(']')), seq(FIXED, chr('['), LENGTHS, chr(']')), seq(DISCARD, chr('['), LENGTHS, chr(']')), seq(READ, chr('['), LENGTHS, chr(']')) ); 
  UNBOUNDED_DESC <= cho( seq(DISCARD, chr(':')), seq(READ, chr(':')) );
  BARCODE <= chr('b');
  UMI <= chr('u');
  DISCARD <= chr('x');
  FIXED <= chr('f');
  READ <= chr('r');
  SEQUENCE <= oom(cls("ATGC"));
  LENGTHS <= cho( seq(LENGTH, chr('-'), LENGTH), LENGTH );
  LENGTH <= seq( cls("123456789"), zom(cls("0123456789")) );
  */

    peg::parser parser(R"(
Specification <- Read1Description Read2Description
Read1Description <- '1{' (UnboundedDescription / (BoundedDescription{1,10} UnboundedDescription{0,1})) '}'
Read2Description <- '2{' (UnboundedDescription / (BoundedDescription{1,10} UnboundedDescription{0,1})) '}'
BoundedDescription <- Barcode'['Lengths']' / UMI'['Lengths']' / Fixed'['Sequence']' / Discard'['Lengths']' / Read'['Lengths']'
UnboundedDescription <-  Discard':' / Read':'
Barcode <-  'b'
UMI <- 'u'
Discard <- 'x'
Fixed <- 'f'
Read <- 'r'
Sequence <- [ATGC]+
Lengths <- (Length '-' Length) / Length
Length <- [1-9][0-9]*
)");

    parser["Read1Description"] = [](const peg::SemanticValues& sv, std::any& dt) {
        (void)sv;
        auto& ps = *std::any_cast<protocol_state*>(dt);
        ps.geo_parts_r1 = ps.geo_parts;
        ps.geo_parts.clear();
    };

    parser["Read2Description"] = [](const peg::SemanticValues& sv, std::any& dt) {
        (void)sv;
        auto& ps = *std::any_cast<protocol_state*>(dt);
        ps.geo_parts_r2 = ps.geo_parts;
        ps.geo_parts.clear();
    };

    parser["UnboundedDescription"] = [](const peg::SemanticValues& sv, std::any& dt) {
        auto& ps = *std::any_cast<protocol_state*>(dt);
        switch (sv.choice()) {
            case 0: {  // x
                ps.geo_parts.push_back({geo_tag_type::DISCARD, -1});
            } break;
            case 1: {  // r
                ps.geo_parts.push_back({geo_tag_type::READ, -1});
            } break;
            default:
                break;
        }
    };

    parser["BoundedDescription"] = [](const peg::SemanticValues& sv, std::any& dt) {
        auto& ps = *std::any_cast<protocol_state*>(dt);
        switch (sv.choice()) {
            case 0: {  // b
                ps.geo_parts.push_back({geo_tag_type::BC, 0});
                ps.geo_parts.back().len = std::any_cast<int64_t>(sv[1]);
            } break;
            case 1: {  // u
                ps.geo_parts.push_back({geo_tag_type::UMI, 0});
                ps.geo_parts.back().len = std::any_cast<int64_t>(sv[1]);
            } break;
            case 2: {  // f
                ps.geo_parts.push_back({geo_tag_type::FIXED, 0});
                ps.geo_parts.back().len = std::any_cast<int64_t>(sv[1]);
            } break;
            case 3: {  // x
                ps.geo_parts.push_back({geo_tag_type::DISCARD, 0});
                ps.geo_parts.back().len = std::any_cast<int64_t>(sv[1]);
            } break;
            case 4: {  // r
                ps.geo_parts.push_back({geo_tag_type::READ, 0});
                ps.geo_parts.back().len = std::any_cast<int64_t>(sv[1]);
            } break;
            default:
                break;
        }
    };

    parser["Lengths"] = [](const peg::SemanticValues& sv, std::any& dt) {
        (void) dt;
        int64_t len{-1};
        switch (sv.choice()) {
            case 0: {
                std::cerr << "variable length barcodes are not currently supported.\n";
            } break;
            default: {
                len = sv.token_to_number<int64_t>();
            } break;
        }
        return len;
    };

    parser["Length"] = [](const peg::SemanticValues& sv, std::any& dt) {
        (void)dt;
        return sv.token_to_number<int64_t>();
    };

    protocol_state ps;
    std::any dt = &ps;
    if (parser.parse(geom, dt)) {
        std::unique_ptr<custom_protocol> p = std::make_unique<custom_protocol>(ps);
        return p;
    } else {
        return nullptr;
    }
}

}  // namespace util
}  // namespace single_cell

class chromium_v3 {
public:
    chromium_v3() = default;
    // copy constructor
    chromium_v3(const chromium_v3& o) = default;

    // We'd really like an std::optional<string&> here, but C++17
    // said no to that.
    std::string* extract_bc(std::string& r1, std::string& r2) {
        (void)r2;
        return (r1.length() >= bc_len) ? (bc.assign(r1, 0, bc_len), &bc) : nullptr;
    }

    // We'd really like an std::optional<string&> here, but C++17
    // said no to that.
    std::string* extract_umi(std::string& r1, std::string& r2) {
        (void)r2;
        return (r1.length() >= (bc_len + umi_len)) ? (umi.assign(r1, bc_len, umi_len), &umi)
                                                   : nullptr;
    }

    std::string* extract_mappable_read(std::string& r1, std::string& r2) {
        (void)r1;
        return &r2;
    }

    bool validate() const { return true; }
    size_t get_bc_len() const { return bc_len; }
    size_t get_umi_len() const { return umi_len; }

private:
    std::string umi;
    std::string bc;
    const size_t bc_len = 16;
    const size_t umi_len = 12;
};

class chromium_v2 {
public:
    chromium_v2() = default;
    // copy constructor
    chromium_v2(const chromium_v2& o) = default;

    // We'd really like an std::optional<string&> here, but C++17
    // said no to that.
    std::string* extract_bc(std::string& r1, std::string& r2) {
        (void)r2;
        return (r1.length() >= bc_len) ? (bc.assign(r1, 0, bc_len), &bc) : nullptr;
    }

    // We'd really like an std::optional<string&> here, but C++17
    // said no to that.
    std::string* extract_umi(std::string& r1, std::string& r2) {
        (void)r2;
        return (r1.length() >= (bc_len + umi_len)) ? (umi.assign(r1, bc_len, umi_len), &umi) : nullptr;
    }

    std::string* extract_mappable_read(std::string& r1, std::string& r2) {
        (void)r1;
        return &r2;
    }

    bool validate() const { return true; }
    size_t get_bc_len() const { return bc_len; }
    size_t get_umi_len() const { return umi_len; }

private:
    std::string umi;
    std::string bc;
    const size_t bc_len = 16;
    const size_t umi_len = 10;
};

struct str_slice {
    int32_t offset{-1};
    int32_t len{-1};
};

class custom_protocol {
public:
    custom_protocol() = default;

    custom_protocol& operator=(const custom_protocol& other) {
        has_biological_read = other.has_biological_read;
        has_umi = other.has_umi;
        has_barcode = other.has_barcode;

        bc_slices_r1 = other.bc_slices_r1;
        umi_slices_r1 = other.umi_slices_r1;
        read_slices_r1 = other.read_slices_r1;

        bc_slices_r2 = other.bc_slices_r2;
        umi_slices_r2 = other.umi_slices_r2;
        read_slices_r2 = other.read_slices_r2;
        return *this;
    }

    custom_protocol(const custom_protocol& other) {
        has_biological_read = other.has_biological_read;
        has_umi = other.has_umi;
        has_barcode = other.has_barcode;
        // std::string bc_buffer;
        // std::string umi_buffer;
        // std::string read_buffer;

        bc_slices_r1 = other.bc_slices_r1;
        umi_slices_r1 = other.umi_slices_r1;
        read_slices_r1 = other.read_slices_r1;

        bc_slices_r2 = other.bc_slices_r2;
        umi_slices_r2 = other.umi_slices_r2;
        read_slices_r2 = other.read_slices_r2;
    }

    custom_protocol(single_cell::util::protocol_state& ps) {
        using single_cell::util::geo_tag_type;
        // convert the list of lengths to a
        // list of offsets and lengths
        int32_t current_offset{0};
        for (auto& gp : ps.geo_parts_r1) {
            int32_t len = static_cast<int32_t>(gp.len);
            if (gp.ttype == geo_tag_type::BC) {
                bc_slices_r1.push_back({current_offset, len});
            } else if (gp.ttype == geo_tag_type::UMI) {
                umi_slices_r1.push_back({current_offset, len});
            } else if (gp.ttype == geo_tag_type::READ) {
                read_slices_r1.push_back({current_offset, len});
            }
            current_offset += gp.len;
        }
        current_offset = 0;
        for (auto& gp : ps.geo_parts_r2) {
            int32_t len = static_cast<int32_t>(gp.len);
            if (gp.ttype == geo_tag_type::BC) {
                bc_slices_r2.push_back({current_offset, len});
            } else if (gp.ttype == geo_tag_type::UMI) {
                umi_slices_r2.push_back({current_offset, len});
            } else if (gp.ttype == geo_tag_type::READ) {
                read_slices_r2.push_back({current_offset, len});
            }
            current_offset += gp.len;
        }

        // bc and umi lengths must be
        // bounded in parsing, so we can
        // accumulate them this way
        bc_len = 0;
        for (auto& p : bc_slices_r1) { bc_len += p.len; }
        for (auto& p : bc_slices_r2) { bc_len += p.len; }

        umi_len = 0;
        for (auto& p : umi_slices_r1) { umi_len += p.len; }
        for (auto& p : umi_slices_r2) { umi_len += p.len; }

        // read lengths can be unbounded
        int64_t read_len{0};
        for (auto& p : read_slices_r1) {
            if (p.len <= 0) {
                read_len = -1;
                break;
            }
            read_len += p.len;
        }
        if (read_len > 0) {
            for (auto& p : read_slices_r2) {
                if (p.len <= 0) {
                    read_len = -1;
                    break;
                }
                read_len += p.len;
            }
        }
    }

    size_t get_bc_len() const { return bc_len; }
    size_t get_umi_len() const { return umi_len; }

    bool validate() const {
        bool valid = has_barcode;
        valid = (valid and has_umi);
        valid = (valid and has_biological_read);
        valid = (valid and (bc_len <= 32));
        valid = (valid and (umi_len <= 32));
        return valid;
    }

    // We'd really like an std::optional<string&> here, but C++17
    // said no to that.
    std::string* extract_bc(std::string& r1, std::string& r2) {
        bc_buffer.clear();
        const auto r1_len = r1.size();
        const auto r2_len = r2.size();
        // first, gather any barcode pieces from r1
        for (auto& bp : bc_slices_r1) {
            // if the read isn't long enough to collect
            // what we want, then return the null pointer.
            size_t check_offset = static_cast<size_t>(bp.offset + bp.len);
            if (check_offset > r1_len) { return nullptr; }
            bc_buffer.append(r1, bp.offset, bp.len);
        }
        // then, gather any umi pieces from r2
        for (auto& bp : bc_slices_r2) {
            // if the read isn't long enough to collect
            // what we want, then return the null pointer.
            size_t check_offset = static_cast<size_t>(bp.offset + bp.len);
            if (check_offset > r2_len) { return nullptr; }
            bc_buffer.append(r2, bp.offset, bp.len);
        }
        return &bc_buffer;
    }

    // We'd really like an std::optional<string&> here, but C++17
    // said no to that.
    std::string* extract_umi(std::string& r1, std::string& r2) {
        umi_buffer.clear();
        const auto r1_len = r1.size();
        const auto r2_len = r2.size();
        // first, gather any umi pieces from r1
        for (auto& up : umi_slices_r1) {
            // if the read isn't long enough to collect
            // what we want, then return the null pointer.
            size_t check_offset = static_cast<size_t>(up.offset + up.len);
            if (check_offset > r1_len) { return nullptr; }
            umi_buffer.append(r1, up.offset, up.len);
        }
        // then, gather any umi pieces from r2
        for (auto& up : umi_slices_r2) {
            // if the read isn't long enough to collect
            // what we want, then return the null pointer.
            size_t check_offset = static_cast<size_t>(up.offset + up.len);
            if (check_offset > r2_len) { return nullptr; }
            umi_buffer.append(r2, up.offset, up.len);
        }
        return &umi_buffer;
    }

    std::string* extract_mappable_read(std::string& r1, std::string& r2) {
        // currently, we make the assumption that the read is either
        // all of r1 or all of r2 or uses only r1 or r2.  This is
        // because we return a string pointer from here. If the read can
        // return sequence from both r1 and r2, then we should return a
        // pair of string pointers.
        bool uses_r2 = !read_slices_r2.empty();
        bool uses_r1 = !read_slices_r1.empty();
        if (uses_r2 and (read_slices_r2[0].offset == 0 and read_slices_r2[0].len == -1)) {
            return &r2;
        } else if (uses_r1 and (read_slices_r1[0].offset == 0 and read_slices_r1[0].len == -1)) {
            return &r1;
        } else if (uses_r2) {
            read_buffer.clear();
            size_t r2_len = r2.size();
            for (auto& up : read_slices_r2) {
                size_t check_offset = (up.len > 0) ? (up.offset + up.len - 1) : up.offset;
                if (check_offset >= r2_len) { return nullptr; }
                size_t len = (up.len == -1) ? std::string::npos : up.len;
                umi_buffer.append(r2, up.offset, len);
            }
        } else if (uses_r1) {
            read_buffer.clear();
            size_t r1_len = r1.size();
            for (auto& up : read_slices_r1) {
                size_t check_offset = (up.len > 0) ? (up.offset + up.len - 1) : up.offset;
                if (check_offset >= r1_len) { return nullptr; }
                size_t len = (up.len == -1) ? std::string::npos : up.len;
                umi_buffer.append(r1, up.offset, len);
            }
        }
        return &read_buffer;
    }

    friend std::ostream& operator<<(std::ostream& os, const custom_protocol& p);

private:
    bool has_biological_read{false};
    bool has_umi{false};
    bool has_barcode{false};
    size_t bc_len{0};
    size_t umi_len{0};
    std::string bc_buffer;
    std::string umi_buffer;
    std::string read_buffer;

    itlib::small_vector<str_slice, 8> bc_slices_r1;
    itlib::small_vector<str_slice, 8> umi_slices_r1;
    itlib::small_vector<str_slice, 8> read_slices_r1;

    itlib::small_vector<str_slice, 8> bc_slices_r2;
    itlib::small_vector<str_slice, 8> umi_slices_r2;
    itlib::small_vector<str_slice, 8> read_slices_r2;
};

#endif  // __SC_UTIL_HPP__
