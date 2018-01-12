// Copyright 2017 Global Phasing Ltd.
//
// Read PDB format into a Structure from model.hpp.
// Based on the format spec:
// https://www.wwpdb.org/documentation/file-format-content/format33/v3.3.html
// + support for two-character chain IDs (columns 21 and 22)
// + read segment ID (columns 73-76)
// + ignore atom serial number (compatible with the cctbx hybrid-36 extension)
// + hybrid-36 sequence id for sequences longer than 9999 (no such examples)

#ifndef GEMMI_PDB_HPP_
#define GEMMI_PDB_HPP_

#include <algorithm>  // for find_if_not, swap
#include <cctype>     // for isspace
#include <cstdio>     // for FILE, size_t
#include <cstdlib>    // for strtol
#include <cstring>    // for memcpy, strstr
#include <map>        // for map
#include <memory>     // for unique_ptr
#include <string>     // for string
#include <vector>     // for vector

#include "model.hpp"
#include "util.hpp"

namespace gemmi {

namespace pdb_impl {

inline std::string rtrimmed(std::string s) {
  auto p = std::find_if_not(s.rbegin(), s.rend(),
                            [](int c) { return std::isspace(c); });
  s.erase(p.base(), s.end());
  return s;
}

inline int read_int(const char* p, int field_length) {
  int sign = 1;
  int n = 0;
  int i = 0;
  while (i < field_length && std::isspace(p[i]))
    ++i;
  if (p[i] == '-') {
    ++i;
    sign = -1;
  } else if (p[i] == '+') {
    ++i;
  }
  for (; i < field_length && p[i] >= '0' && p[i] <= '9'; ++i) {
    n = n * 10 + (p[i] - '0');
  }
  return sign * n;
}

template<int N> int read_base36(const char* p) {
  char zstr[N+1] = {0};
  std::memcpy(zstr, p, N);
  return std::strtol(zstr, NULL, 36);
}

inline double read_double(const char* p, int field_length) {
  int sign = 1;
  double d = 0;
  int i = 0;
  while (i < field_length && std::isspace(p[i]))
    ++i;
  if (p[i] == '-') {
    ++i;
    sign = -1;
  } else if (p[i] == '+') {
    ++i;
  }
  for (; i < field_length && p[i] >= '0' && p[i] <= '9'; ++i)
    d = d * 10 + (p[i] - '0');
  if (i < field_length && p[i] == '.') {
    double mult = 0.1;
    for (++i; i < field_length && p[i] >= '0' && p[i] <= '9'; ++i, mult *= 0.1)
      d += mult * (p[i] - '0');
  }
  return sign * d;
}

inline std::string read_string(const char* p, int field_length) {
  // left trim
  while (field_length != 0 && std::isspace(*p)) {
    ++p;
    --field_length;
  }
  // EOL/EOF ends the string
  for (int i = 0; i < field_length; ++i)
    if (p[i] == '\n' || p[i] == '\r' || p[i] == '\0') {
      field_length = i;
      break;
    }
  // right trim
  while (field_length != 0 && std::isspace(p[field_length-1]))
    --field_length;
  return std::string(p, field_length);
}

// Compare the first 4 letters of s, ignoring case, with uppercase record.
// Both args must have at least 3+1 chars. ' ' and NUL are equivalent in s.
inline bool is_record_type(const char* s, const char* record) {
  return ((s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3]) & ~0x20202020) ==
          (record[0] << 24 | record[1] << 16 | record[2] << 8 | record[3]);
}

class EntitySetter {
public:
  explicit EntitySetter(Structure& st) : st_(st) {}
  Entity* set_for_chain(const std::string& chain_name, EntityType type) {
    auto it = chain_to_ent_.find(chain_name);
    if (it != chain_to_ent_.end())
      return it->second;
    Entity *ent = new Entity{"", type, PolymerType::NA, {}};
    st_.entities.emplace_back(ent);
    chain_to_ent_[chain_name] = ent;
    return ent;
  }
  void finalize() {
    for (auto i = st_.entities.begin(); i != st_.entities.end(); ++i)
      for (auto j = i + 1; j != st_.entities.end(); ++j)
        if (same_entity((*j)->sequence, (*i)->sequence)) {
          for (auto& ce : chain_to_ent_)
            if (ce.second == j->get())
              ce.second = i->get();
          j = st_.entities.erase(j) - 1;
        }
    // set all entity pointers in chains
    for (Model& mod : st_.models)
      for (Chain& ch : mod.chains)
        ch.entity = set_for_chain(ch.name, EntityType::Unknown);
    // set unique IDs
    int serial = 1;
    for (auto& ent : st_.entities)
      ent->id = std::to_string(serial++);
  }

private:
  Structure& st_;
  std::map<std::string, Entity*> chain_to_ent_;

  // PDB format has no equivalent of mmCIF entity. Here we assume that
  // identical SEQRES means the same entity.
  bool same_entity(const Sequence& a, const Sequence& b) const {
    if (a.empty() || a.size() != b.size())
      return false;
    for (size_t i = 0; i != a.size(); ++i)
      if (a[i].mon != b[i].mon)
        return false;
    return true;
  }
};


// The standard charge format is 2+, but some files have +2.
inline signed char read_charge(char digit, char sign) {
  if (sign == ' ' && digit == ' ')  // by far the most common case
    return 0;
  if (sign >= '0' && sign <= '9')
    std::swap(digit, sign);
  if (digit >= '0' && digit <= '9') {
    if (sign != '+' && sign != '-' && sign != '\0' && !std::isspace(sign))
      fail("Wrong format for charge: " +
           std::string(1, digit) + std::string(1, sign));
    return (digit - '0') * (sign == '-' ? -1 : 1);
  }
  // if we are here the field should be blank, but maybe better not to check
  return 0;
}

inline int read_matrix(Mat4x4& matrix, char* line, int len) {
  if (len < 46)
    return 0;
  char n = line[5] - '0';
  if (n >= 1 && n <= 3) {
    matrix.x[n-1] = read_double(line+10, 10);
    matrix.y[n-1] = read_double(line+20, 10);
    matrix.z[n-1] = read_double(line+30, 10);
    matrix.w[n-1] = read_double(line+45, 10);
  }
  return n;
}

inline ResidueId::SNIC read_snic(const char* s) {
  // We support hybrid-36 extension, although it is never used in practice
  // as 9999 residues per chain are enough.
  return { s[0] < 'A' ? read_int(s, 4) : read_base36<4>(s) - 466560 + 10000,
           s[4] == ' ' ? '\0' : s[4] };
}

struct FileInput {
  std::FILE* f;
  char* gets(char* line, int size) { return std::fgets(line, size, f); }
  int getc() { return std::fgetc(f); }
};

// overloaded for gzFile in gz.hpp
template<typename Input>
inline size_t copy_line_from_stream(char* line, int size, Input&& in) {
  if (!in.gets(line, size))
    return 0;
  size_t len = std::strlen(line);
  // If a line is longer than size we discard the rest of it.
  if (len > 0 && line[len-1] != '\n')
    for (int c = in.getc(); c != 0 && c != EOF && c != '\n'; c = in.getc())
      continue;
  return len;
}

void process_conn(Structure& st, const std::vector<std::string>& conn_records) {
  int disulf_count = 0;
  for (const std::string& record : conn_records) {
    const char* r = record.c_str();
    ResidueId rid(read_snic(r + 17), read_string(r + 11, 3));
    if (*r == 'S' || *r == 's') { // SSBOND
      ResidueId rid2(read_snic(r + 31), read_string(r + 25, 3));
      for (Model& model : st.models) {
        Chain* chain1 = model.find_chain(read_string(r + 14, 2));
        Chain* chain2 = model.find_chain(read_string(r + 28, 2));
        if (chain1 && chain2) {
          Connection c;
          c.id = "disulf" + std::to_string(++disulf_count);
          c.type = Connection::Disulf;
          c.res1 = chain1->find_residue(rid);
          c.res2 = chain2->find_residue(rid2);
          if (c.res1 && c.res2) {
            c.res1->conn.push_back("1 " + c.id);
            c.res2->conn.push_back("2 " + c.id);
            model.connections.emplace_back(c);
          }
        }
      }
    } else if (*r == 'C' || *r == 'c') { // CISPEP
      for (Model& model : st.models)
        if (Chain* chain = model.find_chain(read_string(r + 14, 2)))
          if (Residue* res = chain->find_residue(rid))
            res->is_cis = true;
    }
  }
}

template<typename Input>
Structure read_pdb_from_line_input(Input&& infile, const std::string& source) {
  using namespace pdb_impl;
  int line_num = 0;
  auto wrong = [&line_num](const std::string& msg) {
    fail("Problem in line " + std::to_string(line_num) + ": " + msg);
  };
  Structure st;
  st.name = gemmi::path_basename(source);
  std::vector<std::string> has_ter;
  std::vector<std::string> conn_records;
  Model *model = st.find_or_add_model("1");
  Chain *chain = nullptr;
  Residue *resi = nullptr;
  EntitySetter ent_setter(st);
  char line[88] = {0};
  Mat4x4 matrix = linalg::identity;
  while (size_t len = copy_line_from_stream(line, 82, infile)) {
    ++line_num;
    if (is_record_type(line, "ATOM") || is_record_type(line, "HETATM")) {
      if (len < 77) // should we allow missing element
        wrong("The line is too short to be correct:\n" + std::string(line));
      std::string chain_name = read_string(line+20, 2);
      if (!chain || chain_name != chain->auth_name) {
        if (!model)
          wrong("ATOM/HETATM between models");
        // if this chain was TER'ed we use a separate chain for the rest.
        bool ter = gemmi::in_vector(model->name + "/" + chain_name, has_ter);
        chain = model->find_or_add_chain(chain_name + (ter ? "_H" : ""));
        chain->auth_name = chain_name;
        resi = nullptr;
      }

      ResidueId rid(read_snic(line+22), read_string(line+17, 3));
      // Non-standard but widely used 4-character segment identifier.
      // Left-justified, and may include a space in the middle.
      // The segment may be a portion of a chain or a complete chain.
      rid.segment = read_string(line+72, 4);
      if (!resi || !resi->matches(rid))
        resi = chain->find_or_add_residue(rid);

      Atom atom;
      atom.name = read_string(line+12, 4);
      atom.group = line[0] & ~0x20;
      atom.altloc = line[16] == ' ' ? '\0' : line[16];
      atom.charge = (len > 78 ? read_charge(line[78], line[79]) : 0);
      atom.element = gemmi::Element(line+76);
      atom.pos.x = read_double(line+30, 8);
      atom.pos.y = read_double(line+38, 8);
      atom.pos.z = read_double(line+46, 8);
      atom.occ = (float) read_double(line+54, 6);
      atom.b_iso = (float) read_double(line+60, 6);
      resi->atoms.emplace_back(atom);

    } else if (is_record_type(line, "ANISOU")) {
      if (!model || !chain || !resi || resi->atoms.empty())
        wrong("ANISOU record not directly after ATOM/HETATM.");
      // We assume that ANISOU refers to the last atom.
      // Can it not be the case?
      Atom &atom = resi->atoms.back();
      if (atom.u11 != 0.)
        wrong("Duplicated ANISOU record or not directly after ATOM/HETATM.");
      atom.u11 = read_int(line+28, 7) * 1e-4f;
      atom.u22 = read_int(line+35, 7) * 1e-4f;
      atom.u33 = read_int(line+42, 7) * 1e-4f;
      atom.u12 = read_int(line+49, 7) * 1e-4f;
      atom.u13 = read_int(line+56, 7) * 1e-4f;
      atom.u23 = read_int(line+63, 7) * 1e-4f;

    } else if (is_record_type(line, "REMARK")) {
      // ignore for now

    } else if (is_record_type(line, "CONECT")) {
      // ignore for now

    } else if (is_record_type(line, "SEQRES")) {
      std::string chain_name = read_string(line+10, 2);
      Entity* ent = ent_setter.set_for_chain(chain_name, EntityType::Polymer);
      for (int i = 19; i < 68; i += 4) {
        std::string res_name = read_string(line+i, 3);
        if (!res_name.empty())
          ent->sequence.emplace_back(res_name);
      }

    } else if (is_record_type(line, "HEADER")) {
      if (len > 50)
        st.info["_struct_keywords.pdbx_keywords"] =
                                    rtrimmed(std::string(line+10, 40));
      if (len > 59) { // date in PDB has format 28-MAR-07
        std::string date(line+50, 9);
        const char months[] = "JAN01FEB02MAR03APR04MAY05JUN06"
                              "JUL07AUG08SEP09OCT10NOV11DEC122222";
        const char* m = strstr(months, date.substr(3, 3).c_str());
        st.info["_pdbx_database_status.recvd_initial_deposition_date"] =
          (date[7] > '6' ? "19" : "20") + date.substr(7, 2) + "-" +
          (m ? std::string(m+3, 2) : "??") + "-" + date.substr(0, 2);
      }
      if (len > 66)
        st.info["_entry.id"] = std::string(line+62, 4);

    } else if (is_record_type(line, "TITLE")) {
      if (len > 10)
        st.info["_struct.title"] += rtrimmed(std::string(line+10, len-10-1));

    } else if (is_record_type(line, "KEYWDS")) {
      if (len > 10)
        st.info["_struct_keywords.text"] +=
                                    rtrimmed(std::string(line+10, len-10-1));

    } else if (is_record_type(line, "EXPDTA")) {
      if (len > 10)
        st.info["_exptl.method"] += rtrimmed(std::string(line+10, len-10-1));

    } else if (is_record_type(line, "CRYST1")) {
      if (len > 54)
        st.cell.set(read_double(line+6, 9),
                    read_double(line+15, 9),
                    read_double(line+24, 9),
                    read_double(line+33, 7),
                    read_double(line+40, 7),
                    read_double(line+47, 7));
      if (len > 56)
        st.sg_hm = read_string(line+55, 11);
      if (len > 67) {
        std::string z = read_string(line+66, 4);
        if (!z.empty())
          st.info["_cell.Z_PDB"] = z;
      }
    } else if (is_record_type(line, "MTRIXn")) {
      if (read_matrix(matrix, line, len) == 3 &&
          matrix != Mat4x4(linalg::identity)) {
        bool given = len > 59 && line[59] == '1';
        st.ncs.push_back({read_string(line+7, 3), given, matrix});
        matrix = linalg::identity;
      }
    } else if (is_record_type(line, "MODEL")) {
      if (model && chain)
        wrong("MODEL without ENDMDL?");
      std::string name = std::to_string(read_int(line+10, 4));
      model = st.find_or_add_model(name);
      if (!model->chains.empty())
        wrong("duplicate MODEL number: " + name);
      chain = nullptr;

    } else if (is_record_type(line, "ENDMDL")) {
      model = nullptr;
      chain = nullptr;

    } else if (is_record_type(line, "TER")) { // finishes polymer chains
      if (chain)
        has_ter.emplace_back(model->name + "/" + chain->name);
      chain = nullptr;

    } else if (is_record_type(line, "SCALEn")) {
      if (read_matrix(matrix, line, len) == 3) {
        st.cell.set_matrices_from_fract(matrix);
        matrix = linalg::identity;
      }

    } else if (is_record_type(line, "ORIGX")) {
      if (read_matrix(matrix, line, len) == 3)
        st.origx = matrix;

    } else if (is_record_type(line, "SSBOND")) {
      std::string record(line);
      if (record.length() > 34)
        conn_records.emplace_back(record);

    } else if (is_record_type(line, "CISPEP")) {
      std::string record(line);
      if (record.length() > 21)
        conn_records.emplace_back(record);
    } else if (is_record_type(line, "END")) {  // NUL == ' ' & ~0x20
      break;
    }
  }

  ent_setter.finalize();
  for (Model& mod : st.models)
    for (Chain& ch : mod.chains) {
      if (gemmi::in_vector(mod.name + "/" + ch.name, has_ter))
        ch.entity->type = EntityType::Polymer;
    }
  st.finish();

  process_conn(st, conn_records);

  return st;
}

}  // namespace pdb_impl

inline Structure read_pdb_file(const std::string& path) {
  auto f = gemmi::file_open(path.c_str(), "r");
  return read_pdb_from_line_input(pdb_impl::FileInput{f.get()}, path);
}

// A function for transparent reading of stdin and/or gzipped files.
template<typename T>
inline Structure read_pdb(T&& input) {
  if (input.is_stdin())
    return read_pdb_from_line_input(pdb_impl::FileInput{stdin}, "stdin");
  if (auto line_input = input.get_line_stream())
    return pdb_impl::read_pdb_from_line_input(line_input, input.path());
  return read_pdb_file(input.path());
}

} // namespace gemmi
#endif
// vim:sw=2:ts=2:et
