#pragma once
// Parameters: one registry that gives every parameter a name, a value, a default and a help line.
//
// The same name is used everywhere:
//   code       cfg.gcm.size = 15;
//   file       gcm.size = 15
//   command    --gcm.size=15        (also: --gcm.size 15, as irace passes it)
//
// Params does not own the values: it keeps references to them (fields of gso::Config, or members
// of the problem adapter). Build it, use it, and drop it while those objects are alive.
//
// Components are written as text specs:  name(key=value, key=value), for example "ucb1(c=1.0)".

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gso {

// A wrong parameter name or value. The message says what is wrong and how to fix it.
struct ConfigError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

namespace detail {

inline std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

// Edit distance, used to suggest the right name after a typo.
inline std::size_t edit_distance(const std::string& a, const std::string& b) {
  std::vector<std::size_t> row(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
  for (std::size_t i = 1; i <= a.size(); ++i) {
    std::size_t diag = row[0];
    row[0] = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
      const std::size_t up = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
      diag = up;
    }
  }
  return row[b.size()];
}

inline std::string closest_name(const std::string& wrong, const std::vector<std::string>& names) {
  std::string best;
  std::size_t best_d = 4;  // suggest only close names
  for (const auto& n : names) {
    const std::size_t d = edit_distance(wrong, n);
    if (d < best_d) { best_d = d; best = n; }
  }
  return best;
}

inline std::string format_number(double v) {
  std::ostringstream os;
  os << std::setprecision(10) << v;
  return os.str();
}

template <class T>
std::string to_text(const T& v) {
  if constexpr (std::is_same_v<T, bool>) {
    return v ? "true" : "false";
  } else if constexpr (std::is_floating_point_v<T>) {
    return format_number(static_cast<double>(v));
  } else if constexpr (std::is_integral_v<T>) {
    return std::to_string(v);
  } else {
    return v;  // std::string
  }
}

template <class T>
T from_text(const std::string& name, const std::string& text) {
  const std::string t = trim(text);
  auto fail = [&](const char* expected) {
    return ConfigError("parameter '" + name + "': '" + t + "' is not " + expected);
  };
  if constexpr (std::is_same_v<T, bool>) {
    std::string low = t;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });
    if (low == "true" || low == "1" || low == "yes" || low == "on") return true;
    if (low == "false" || low == "0" || low == "no" || low == "off") return false;
    throw fail("true/false");
  } else if constexpr (std::is_floating_point_v<T>) {
    std::size_t used = 0;
    double v = 0;
    try { v = std::stod(t, &used); } catch (...) { throw fail("a number"); }
    if (used != t.size()) throw fail("a number");
    return static_cast<T>(v);
  } else if constexpr (std::is_integral_v<T>) {
    std::size_t used = 0;
    long long v = 0;
    try { v = std::stoll(t, &used); } catch (...) { throw fail("an integer"); }
    if (used != t.size()) throw fail("an integer");
    return static_cast<T>(v);
  } else {
    return t;  // std::string
  }
}

}  // namespace detail

// ---------------------------------------------------------------------------------------------
// Spec: a component written as  name(key=value, ...)
// ---------------------------------------------------------------------------------------------
struct Spec {
  std::string name;
  std::vector<std::pair<std::string, std::string>> args;

  static Spec parse(const std::string& text) {
    Spec spec;
    const std::string t = detail::trim(text);
    const auto open = t.find('(');
    if (open == std::string::npos) {
      spec.name = t;
    } else {
      if (t.back() != ')') throw ConfigError("'" + t + "': missing ')'. Expected name(key=value, ...)");
      spec.name = detail::trim(t.substr(0, open));
      std::stringstream inner(t.substr(open + 1, t.size() - open - 2));
      std::string item;
      while (std::getline(inner, item, ',')) {
        if (detail::trim(item).empty()) continue;
        const auto eq = item.find('=');
        if (eq == std::string::npos)
          throw ConfigError("'" + t + "': argument '" + detail::trim(item) + "' must be key=value");
        const std::string key = detail::trim(item.substr(0, eq));
        const std::string value = detail::trim(item.substr(eq + 1));
        if (key.empty() || value.empty())
          throw ConfigError("'" + t + "': argument '" + detail::trim(item) + "' must be key=value");
        spec.args.emplace_back(key, value);
      }
    }
    if (spec.name.empty()) throw ConfigError("'" + t + "': missing component name");
    return spec;
  }

  bool has(const std::string& key) const {
    return std::any_of(args.begin(), args.end(), [&](const auto& kv) { return kv.first == key; });
  }

  std::string text(const std::string& key, const std::string& fallback) const {
    for (const auto& [k, v] : args)
      if (k == key) return v;
    return fallback;
  }

  double number(const std::string& key, double fallback) const {
    return has(key) ? detail::from_text<double>(name + "(" + key + ")", text(key, "")) : fallback;
  }

  // Rejects arguments that the component does not understand (typos would be silently ignored).
  void allow_only(std::initializer_list<const char*> allowed) const {
    for (const auto& kv : args) {
      bool ok = false;
      std::string list;
      for (const char* a : allowed) {
        ok = ok || kv.first == a;
        list += std::string(list.empty() ? "" : ", ") + a;
      }
      if (!ok)
        throw ConfigError("'" + name + "' has no argument '" + kv.first + "'" +
                          (list.empty() ? " (it takes none)" : ". Valid arguments: " + list));
    }
  }
};

// ---------------------------------------------------------------------------------------------
// Params: the registry
// ---------------------------------------------------------------------------------------------
class Params {
 public:
  // Registers a parameter. T: bool, an integer type, a floating-point type or std::string.
  // `fallback` is the default shown by help() and print(); empty = the current value.
  template <class T>
  void add(const std::string& short_name, T& value, const std::string& help, const std::string& range = "",
           const std::string& fallback = "") {
    const std::string name = prefix_ + short_name;
    Entry e;
    e.name = name;
    e.help = help;
    e.range = range;
    e.default_value = fallback.empty() ? detail::to_text(value) : fallback;
    e.get = [&value] { return detail::to_text(value); };
    e.set = [&value, name](const std::string& text) { value = detail::from_text<T>(name, text); };
    for (auto& old : entries_)
      if (old.name == name) { old = std::move(e); return; }
    entries_.push_back(std::move(e));
  }

  // Names added from now on start with `p` (the adapter's parameters are added under "problem.").
  void prefix(const std::string& p) { prefix_ = p; }

  bool contains(const std::string& name) const { return find(name) != nullptr; }

  std::string get(const std::string& name) const { return entry(name).get(); }

  void set(const std::string& name, const std::string& value) { entry(name).set(value); }

  // Reads a file with lines "name = value". '#' starts a comment.
  void load_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ConfigError("cannot open configuration file '" + path + "'");
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
      ++number;
      const auto hash = line.find('#');
      if (hash != std::string::npos) line.erase(hash);
      line = detail::trim(line);
      if (line.empty()) continue;
      const auto eq = line.find('=');
      if (eq == std::string::npos)
        throw ConfigError(path + ":" + std::to_string(number) + ": expected 'name = value'");
      try {
        set(detail::trim(line.substr(0, eq)), line.substr(eq + 1));
      } catch (const ConfigError& err) {
        throw ConfigError(path + ":" + std::to_string(number) + ": " + err.what());
      }
    }
  }

  // Reads command-line arguments:
  //   --name=value | --name value | --config=<file> | --help
  // Arguments that do not start with "--" are returned in `positional` (e.g. an instance path).
  // Returns false if --help was given; the help is printed to `out`.
  bool load_args(int argc, char** argv, std::vector<std::string>* positional = nullptr,
                 std::ostream& out = std::cout) {
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg.rfind("--", 0) != 0) {
        if (positional) positional->push_back(arg);
        continue;
      }
      if (arg == "--help" || arg == "-h") {
        help(out);
        return false;
      }
      std::string name = arg.substr(2), value;
      const auto eq = name.find('=');
      if (eq != std::string::npos) {
        value = name.substr(eq + 1);
        name.erase(eq);
      } else if (i + 1 < argc) {
        value = argv[++i];
      } else {
        throw ConfigError("argument '" + arg + "' has no value");
      }
      if (name == "config") load_file(value);
      else if (name == "preset") continue;  // handled by gso::configure before this call
      else set(name, value);
    }
    return true;
  }

  // Prints "name = value   # help" for every parameter, grouped by the prefix before the first '.'.
  // Values that differ from their default are followed by "(default: ...)".
  void print(std::ostream& os) const {
    std::size_t width = 0;
    for (const auto& e : entries_) width = std::max(width, e.name.size() + 3 + e.get().size());
    std::string group = "\x01";
    for (const auto& e : entries_) {
      const std::string g = group_of(e.name);
      if (g != group) {
        group = g;
        const std::string title = g.empty() ? "general" : g;
        os << "# -- " << title << " " << std::string(title.size() < 56 ? 56 - title.size() : 4, '-') << "\n";
      }
      const std::string value = e.get();
      std::string line = e.name + " = " + value;
      os << line << std::string(width + 2 - line.size(), ' ') << "# " << e.help;
      if (value != e.default_value) os << "  (default: " << e.default_value << ")";
      os << "\n";
    }
  }

  // Prints every parameter with its default, range and meaning.
  void help(std::ostream& os) const {
    os << "Parameters (--name=value, or 'name = value' lines in a file given with --config=<file>):\n";
    std::string group = "\x01";
    for (const auto& e : entries_) {
      const std::string g = group_of(e.name);
      if (g != group) {
        group = g;
        os << "\n  [" << (g.empty() ? "general" : g) << "]\n";
      }
      os << "  --" << std::left << std::setw(26) << e.name << " " << e.help << "\n"
         << "    " << std::setw(26) << "" << " default: " << e.default_value;
      if (!e.range.empty()) os << "   range: " << e.range;
      os << "\n";
    }
  }

  std::vector<std::string> names() const {
    std::vector<std::string> n;
    for (const auto& e : entries_) n.push_back(e.name);
    return n;
  }

 private:
  struct Entry {
    std::string name, help, range, default_value;
    std::function<std::string()> get;
    std::function<void(const std::string&)> set;
  };

  static std::string group_of(const std::string& name) {
    const auto dot = name.find('.');
    return dot == std::string::npos ? "" : name.substr(0, dot);
  }

  const Entry* find(const std::string& name) const {
    for (const auto& e : entries_)
      if (e.name == name) return &e;
    return nullptr;
  }

  const Entry& entry(const std::string& name) const {
    if (const Entry* e = find(name)) return *e;
    const std::string guess = detail::closest_name(name, names());
    throw ConfigError("unknown parameter '" + name + "'" +
                      (guess.empty() ? std::string(". Use --help to list them") : ". Did you mean '" + guess + "'?"));
  }

  std::vector<Entry> entries_;
  std::string prefix_;
};

}  // namespace gso
