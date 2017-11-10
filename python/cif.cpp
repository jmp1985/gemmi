// Copyright 2017 Global Phasing Ltd.

#include "gemmi/numb.hpp"
#include "gemmi/cifdoc.hpp"
#include "gemmi/gz.hpp"
#include "gemmi/cif.hpp"
#include "gemmi/to_cif.hpp"
#include "gemmi/to_json.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace gemmi;
using namespace gemmi::cif;

void init_cif(py::module& cif) {
  py::class_<Document>(cif, "Document")
    .def(py::init<>())
    .def("__len__", [](const Document& d) { return d.blocks.size(); })
    .def("__iter__", [](const Document& d) {
        return py::make_iterator(d.blocks);
    }, py::keep_alive<0, 1>())
    .def("__getitem__", [](const Document& d, const std::string& name)
                          -> const Block& {
        const Block* b = d.find_block(name);
        if (!b)
          throw py::key_error("block '" + name + "' does not exist");
        return *b;
    }, py::arg("name"), py::return_value_policy::reference_internal)
    .def("__getitem__", [](const Document& d, int index) -> const Block& {
        return d.blocks.at(index >= 0 ? index : index + d.blocks.size());
    }, py::arg("index"), py::return_value_policy::reference_internal)
    .def("__delitem__", [](Document &d, int index) {
        if (index < 0)
          index += d.blocks.size();
        if (index < 0 || static_cast<size_t>(index) >= d.blocks.size())
          throw py::index_error();
        d.blocks.erase(d.blocks.begin() + index);
    }, py::arg("index"))
    .def("clear", &Document::clear)
    .def("sole_block", &Document::sole_block,
         "Returns the only block if there is exactly one")
    .def("find_block", &Document::find_block, py::arg("name"),
         py::return_value_policy::reference_internal)
    .def("write_file", &write_to_file, py::arg("filename"),
         "Write data to a CIF file.")
    .def("as_json", [](const Document& d) {
        std::ostringstream os;
        JsonWriter(os).write_json(d);
        return os.str();
    }, "Returns JSON representation in a string.");

  py::class_<Block>(cif, "Block")
    .def(py::init<>())
    .def_readonly("name", &Block::name)
    .def("find_value", &Block::find_value, py::arg("tag"),
         py::return_value_policy::reference)
    .def("find_loop", &Block::find_loop, py::arg("tag"))
    .def("find", (TableView (Block::*)(const std::string&) const) &Block::find,
         py::arg("tag"))
    .def("find", (TableView (Block::*)(const std::string&,
            const std::vector<std::string>&) const) &Block::find,
         py::arg("prefix"), py::arg("tags"))
    .def("find", (TableView (Block::*)(const std::vector<std::string>&) const)
                 &Block::find,
         py::arg("tags"))
    .def("delete_category", &Block::delete_category, py::arg("prefix"),
         "End mmCIF category with the dot: block.delete_category('_exptl.')")
    .def("__repr__", [](const Block &self) {
        return "<gemmi.cif.Block " + self.name + ">";
    });

  py::class_<Loop> lp(cif, "Loop");
  lp.def(py::init<>())
    .def("width", &Loop::width, "Returns number of columns")
    .def("length", &Loop::length, "Returns number of rows")
    .def_readonly("tags", &Loop::tags)
    .def("__iter__", [](const Loop& self) {
        return py::make_iterator(self);
    }, py::keep_alive<0, 1>())
    .def("val", &Loop::val, py::arg("row"), py::arg("col"))
    .def("__repr__", [](const Loop &self) {
        return "<gemmi.cif.Loop " + std::to_string(self.length()) + " x " +
                                    std::to_string(self.width()) + ">";
    });

  py::class_<LoopTag>(cif, "LoopTag")
    .def_readonly("tag", &LoopTag::tag);

  py::class_<Loop::Span>(lp, "Span")
    .def("__len__", &Loop::Span::size)
    .def("__getitem__", &Loop::Span::at)
    .def("__iter__", [](const Loop::Span& self) {
        return py::make_iterator(self);
    }, py::keep_alive<0, 1>())
    .def("__repr__", [](const Loop::Span& self) {
        return "<gemmi.cif.Loop.Span: " + join_str(self, " ") + ">";
    });

  py::class_<LoopColumn>(cif, "LoopColumn")
    .def(py::init<>())
    .def_readonly("loop", &LoopColumn::loop)
    .def_readwrite("col", &LoopColumn::col)
    .def("__iter__", [](const LoopColumn& self) {
        return py::make_iterator(self);
    }, py::keep_alive<0, 1>())
    .def("__bool__", [](const LoopColumn &self) -> bool { return self.loop; })
    .def("__repr__", [](const LoopColumn &self) {
        return "<gemmi.cif.LoopColumn " +
        (self.loop ? self.loop->tags[self.col].tag + " length " +
                     std::to_string(self.loop->length()) : "nil") + ">";
    });

  py::class_<TableView> lt(cif, "TableView");
  lt.def(py::init<>())
    .def_readonly("loop", &TableView::loop)
    .def_readonly("cols", &TableView::cols)
    .def("find_row", &TableView::find_row, py::keep_alive<0, 1>())
    .def("__iter__", [](const TableView& self) {
        return py::make_iterator(self, py::keep_alive<0, 1>());
    }, py::keep_alive<0, 1>())
    .def("__getitem__", &TableView::at, py::keep_alive<0, 1>())
    .def("__bool__", &TableView::ok)
    .def("__len__", &TableView::length)
    .def("__repr__", [](const TableView& self) {
        return "<gemmi.cif.TableView " +
               (self.ok() ? std::to_string(self.length()) + " x " +
                            std::to_string(self.cols.size())
                          : "nil") +
               ">";
    });

  py::class_<TableView::Row>(lt, "Row")
    .def("str", &TableView::Row::str)
    .def("__len__", &TableView::Row::size)
    .def("__getitem__", &TableView::Row::at)
    .def("__iter__", [](const TableView::Row& self) {
        return py::make_iterator(self);
    }, py::keep_alive<0, 1>())
    .def("__repr__", [](const TableView::Row& self) {
        return "<gemmi.cif.TableView.Row: " + join_str(self, " ") + ">";
    });

  cif.def("read_file", &read_file, py::arg("filename"),
          "Reads a CIF file copying data into Document.");
  cif.def("read", [](const std::string& s) { return read(MaybeGzipped(s)); },
          py::arg("filename"), "Reads normal or gzipped CIF file.");
  cif.def("read_string", &read_string, py::arg("data"),
          "Reads a string as a CIF file.");

  cif.def("as_string", (std::string (*)(const std::string&)) &as_string,
          py::arg("value"), "Get string content (no quotes) from raw string.");
  cif.def("as_number", &as_number, py::arg("value"), py::arg("default")=NAN,
          "Returns float number from string");
  cif.def("as_int", (int (*)(const std::string&)) &as_int, py::arg("value"),
          "Returns int number from string value.");
  cif.def("as_int", (int (*)(const std::string&, int)) &as_int,
          py::arg("value"), py::arg("default"),
          "Returns int number from string value or the second arg if null.");
}
