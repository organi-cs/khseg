// Python bindings. Offsets returned to Python are code point offsets, which
// are the same as Python str indices.
#include <khseg/khseg.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace py = pybind11;

namespace {

struct PyToken {
  std::string text;
  std::uint32_t start;
  std::uint32_t end;
  std::string type;
};

khseg::Algorithm parse_algorithm(const std::string& name) {
  if (name == "viterbi") return khseg::Algorithm::Viterbi;
  if (name == "fmm") return khseg::Algorithm::Forward;
  if (name == "bmm") return khseg::Algorithm::Backward;
  if (name == "bimm") return khseg::Algorithm::Bidirectional;
  throw py::value_error("algorithm must be one of viterbi, fmm, bmm, bimm");
}

khseg::LekTooPolicy parse_lektoo(const std::string& name) {
  if (name == "separate") return khseg::LekTooPolicy::Separate;
  if (name == "attach") return khseg::LekTooPolicy::Attach;
  throw py::value_error("lektoo must be 'separate' or 'attach'");
}

// One workspace per thread, reused across calls.
khseg::Workspace& workspace() {
  thread_local khseg::Workspace ws;
  return ws;
}

class PySegmenter {
 public:
  PySegmenter(std::shared_ptr<const khseg::Dictionary> dict, const std::string& algorithm,
              std::optional<double> unknown_cost, bool merge_unknown, bool normalize,
              const std::string& lektoo) {
    khseg::Options o;
    o.algorithm = parse_algorithm(algorithm);
    o.unknown_cost = unknown_cost;
    o.merge_unknown = merge_unknown;
    o.normalize = normalize;
    o.lektoo = parse_lektoo(lektoo);
    seg_ = khseg::Segmenter(std::move(dict), o);
  }

  std::vector<std::string> segment(const std::string& text) const {
    khseg::Workspace& ws = workspace();
    seg_.segment(text, ws, tokens());
    std::vector<std::string> out;
    out.reserve(tokens().size());
    for (const auto& t : tokens()) {
      if (t.type == khseg::TokenType::Space) continue;
      out.emplace_back(text.substr(t.byte_begin, t.byte_end - t.byte_begin));
    }
    return out;
  }

  std::vector<PyToken> tokenize(const std::string& text) const {
    khseg::Workspace& ws = workspace();
    seg_.segment(text, ws, tokens());
    std::vector<PyToken> out;
    out.reserve(tokens().size());
    for (const auto& t : tokens()) {
      out.push_back({text.substr(t.byte_begin, t.byte_end - t.byte_begin), t.begin, t.end,
                     khseg::to_string(t.type)});
    }
    return out;
  }

  const khseg::Segmenter& get() const { return seg_; }

 private:
  static std::vector<khseg::Token>& tokens() {
    thread_local std::vector<khseg::Token> t;
    return t;
  }

  khseg::Segmenter seg_;
};

std::shared_ptr<const khseg::Dictionary> load(const std::string& path, double alpha,
                                              std::optional<double> unknown_cost) {
  khseg::DictionaryOptions o;
  o.alpha = alpha;
  o.unknown_cost = unknown_cost;
  return std::make_shared<const khseg::Dictionary>(khseg::Dictionary::from_file(path, nullptr, o));
}

}  // namespace

PYBIND11_MODULE(_khseg, m) {
  m.doc() = "Khmer word segmentation (C++ core)";
  m.attr("__version__") = khseg::version();

  py::class_<khseg::Dictionary, std::shared_ptr<khseg::Dictionary>>(m, "Dictionary")
      .def_static(
          "load",
          [](const std::string& path, double alpha, std::optional<double> unknown_cost) {
            return std::const_pointer_cast<khseg::Dictionary>(load(path, alpha, unknown_cost));
          },
          py::arg("path"), py::arg("alpha") = 0.5, py::arg("unknown_cost") = py::none(),
          "Load a TSV or binary .khd dictionary.")
      .def("__len__", &khseg::Dictionary::size)
      .def("__contains__",
           [](const khseg::Dictionary& d, const std::string& w) {
             return d.find(khseg::normalize(khseg::utf8::decode(w))) != khseg::kNoEntry;
           })
      .def("cost",
           [](const khseg::Dictionary& d, const std::string& w) -> std::optional<double> {
             const auto id = d.find(khseg::normalize(khseg::utf8::decode(w)));
             if (id == khseg::kNoEntry) return std::nullopt;
             return d.cost(id);
           })
      .def_property_readonly("unknown_cost", &khseg::Dictionary::default_unknown_cost)
      .def("save_binary", [](const khseg::Dictionary& d, const std::string& path) {
        d.save_binary(path);
      });

  py::class_<PyToken>(m, "Token")
      .def_readonly("text", &PyToken::text)
      .def_readonly("start", &PyToken::start)
      .def_readonly("end", &PyToken::end)
      .def_readonly("type", &PyToken::type)
      .def("__repr__", [](const PyToken& t) {
        return "Token(" + py::repr(py::str(t.text)).cast<std::string>() + ", " +
               std::to_string(t.start) + ", " + std::to_string(t.end) + ", '" + t.type + "')";
      })
      .def("__iter__", [](const PyToken& t) {
        return py::iter(py::make_tuple(t.text, t.start, t.end, t.type));
      });

  py::class_<PySegmenter>(m, "Segmenter")
      .def(py::init([](py::object dictionary, const std::string& algorithm,
                       std::optional<double> unknown_cost, bool merge_unknown, bool normalize,
                       const std::string& lektoo) {
             std::shared_ptr<const khseg::Dictionary> d;
             if (py::isinstance<py::str>(dictionary)) {
               d = load(dictionary.cast<std::string>(), 0.5, std::nullopt);
             } else if (!dictionary.is_none()) {
               d = dictionary.cast<std::shared_ptr<khseg::Dictionary>>();
             }
             return PySegmenter(std::move(d), algorithm, unknown_cost, merge_unknown, normalize,
                                lektoo);
           }),
           py::arg("dictionary") = py::none(), py::arg("algorithm") = "viterbi",
           py::arg("unknown_cost") = py::none(), py::arg("merge_unknown") = true,
           py::arg("normalize") = true, py::arg("lektoo") = "separate")
      .def("segment", &PySegmenter::segment, py::arg("text"),
           py::call_guard<py::gil_scoped_release>(),
           "Split text into words. Spaces are dropped; everything else is kept.")
      .def("tokenize", &PySegmenter::tokenize, py::arg("text"),
           py::call_guard<py::gil_scoped_release>(),
           "All tokens with start/end offsets that are Python str indices.")
      .def_property_readonly("unknown_cost",
                             [](const PySegmenter& s) { return s.get().unknown_cost(); });

  m.def(
      "clusters",
      [](const std::string& text) {
        const auto u = khseg::utf8::decode(text);
        std::vector<std::string> out;
        for (auto c : khseg::split_clusters(u)) out.push_back(khseg::utf8::encode(c));
        return out;
      },
      py::arg("text"), "Split text into Khmer character clusters.");
  m.def(
      "normalize",
      [](const std::string& text) {
        return khseg::utf8::encode(khseg::normalize(khseg::utf8::decode(text)));
      },
      py::arg("text"), "Put the marks of each Khmer cluster in canonical order.");
}
