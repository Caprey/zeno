#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "Task.hpp"

namespace zeno {

namespace detail {
std::string python_evaluate(const std::string &fmtStr,
                            const GenericWorkAttribute &range,
                            const std::vector<const char *> args,
                            const std::string &optionStr) {
  // #if defined(ZENO_WITH_PYTHON3) or ZS_PYTHON_FOUND
  std::string head = "print(\'" + fmtStr + "\'.format(";
  const auto quote = "\"";
  if (args.size()) {
    head += quote + std::string(args[0]) + quote;
    for (int i = 1; i != args.size(); ++i)
      head += std::string(", \"") + std::string(args[i]) + quote;
    head += ", range=";
  } else {
    head += "range=";
  }
  std::string tail = std::string("{}") + fmt::format("), \'{}\')", optionStr);

  Py_Initialize();
  PyRun_SimpleString(
      "import sys\n"
      "from io import StringIO\n"
      "old_stdout = sys.stdout\n" // Backup the original stdout
      "sys.stdout = StringIO()\n" // Replace stdout with a StringIO object
                                  // to capture outputs
  );

  zs::match(
      [&head, &tail](const zs::tuple<int, int, int> &r) {
        int st = zs::get<0>(r), ed = zs::get<1>(r), step = zs::get<2>(r);
        if (ed >= st)
          for (; st <= ed; st += step) {
            auto evalExpr = head + fmt::format(tail, st);
            PyRun_SimpleString(evalExpr.data());
          }
        else
          for (; st >= ed; st += step) {
            auto evalExpr = head + fmt::format(tail, st);
            PyRun_SimpleString(evalExpr.data());
          }
      },
      [&head, &tail](const std::vector<int> &r) {
        for (auto v : r) {
          auto evalExpr = head + fmt::format(tail, v);
          PyRun_SimpleString(evalExpr.data());
        }
      })(range);

  PyObject *sys = PyImport_ImportModule("sys");
  PyObject *stdOut = PyObject_GetAttrString(sys, "stdout");
  PyObject *output = PyObject_GetAttrString(stdOut, "getvalue");
  PyObject *result = PyObject_CallObject(output, NULL);

  // Convert the captured output to a C++ string
  const char *result_cstr = PyUnicode_AsUTF8(result);

  // Restore the original stdout
  PyRun_SimpleString("sys.stdout = old_stdout");

  // Finalize the Python interpreter
  Py_Finalize();

  return std::string(result_cstr);
}

std::string python_evaluate(const std::string &script,
                            const std::shared_ptr<ListObject> &args) {

  Py_Initialize();

  // pass arguments
  std::vector<wchar_t *> as;
  bool invalid = false;
  for (auto &&arg : args->get())
    if (auto ptr = dynamic_cast<StringObject *>(arg.get()); ptr != nullptr)
      as.push_back(Py_DecodeLocale(ptr->get().c_str(), NULL));
    else
      invalid = true;
  // throw std::runtime_error(
  //     "there exists an argument not of StringObject type!");
  PyObject *pyargs = PyList_New(as.size());
  for (int i = 0; i < as.size(); ++i)
    PyList_SetItem(pyargs, i, PyUnicode_FromWideChar(as[i], wcslen(as[i])));

  PyObject *sys = PyImport_ImportModule("sys");
  PyObject_SetAttrString(sys, "argv", pyargs);

  Py_DECREF(pyargs);
  for (auto a : as)
    PyMem_Free(a);

  PyRun_SimpleString(
      "import sys\n"
      "from io import StringIO\n"
      "old_stdout = sys.stdout\n" // Backup the original stdout
      "sys.stdout = StringIO()\n" // Replace stdout with a StringIO object
                                  // to capture outputs
  );

  PyRun_SimpleString(script.data());

  PyObject *stdOut = PyObject_GetAttrString(sys, "stdout");
  PyObject *output = PyObject_GetAttrString(stdOut, "getvalue");
  PyObject *result = PyObject_CallObject(output, NULL);

  // Convert the captured output to a C++ string
  const char *result_cstr = PyUnicode_AsUTF8(result);

  // Restore the original stdout
  PyRun_SimpleString("sys.stdout = old_stdout");

  // Finalize the Python interpreter
  Py_Finalize();

  return std::string(result_cstr);
}
} // namespace detail

struct CommandGenerator : INode {
  void apply() override {
    auto tag = get_input2<std::string>("name_tag");
    if (tag.empty())
      throw std::runtime_error("work name must not be empty!");

    bool verbose = get_input2<bool>("verbose");
    auto cmdFmtStr = get_input2<std::string>("cmd_fmt_string");

    ///
    GenericWorkAttribute range;
    auto inputRange = get_input("range");
    if (auto ptr = dynamic_cast<NumericObject *>(inputRange.get());
        ptr != nullptr) {
      zs::tuple<int, int, int> r;
      if (ptr->is<int>()) {
        zs::get<0>(r) = 0;
        zs::get<1>(r) = ptr->get<int>() - 1;
        zs::get<2>(r) = 1;
      } else if (ptr->is<zeno::vec2i>()) {
        auto tmp = ptr->get<zeno::vec2i>();
        zs::get<0>(r) = tmp[0];
        zs::get<1>(r) = tmp[1];
        zs::get<2>(r) = tmp[1] >= tmp[0] ? 1 : -1;
      } else if (ptr->is<zeno::vec3i>()) {
        auto tmp = ptr->get<zeno::vec3i>();
        zs::get<0>(r) = tmp[0];
        zs::get<1>(r) = tmp[1];
        zs::get<2>(r) = tmp[2];
        if ((tmp[1] > tmp[0] && tmp[2] <= 0) ||
            (tmp[1] < tmp[0] && tmp[2] >= 0) ||
            (tmp[1] == tmp[0] && tmp[2] == 0))
          throw std::runtime_error(
              fmt::format("invalid range specification ({}, {}, {})!", tmp[0],
                          tmp[1], tmp[2]));
      } else
        zs::match([](auto &&v) {
          throw std::runtime_error(fmt::format("invalid numeric range type {}!",
                                               zs::get_var_type_str(v)));
        })(ptr->value);
      //
      range = r;
    } else if (auto ptr = dynamic_cast<ListObject *>(inputRange.get());
               ptr != nullptr) {
      std::vector<int> r;
      for (auto &&arg : ptr->get())
        if (auto ptr = dynamic_cast<NumericObject *>(arg.get()); ptr != nullptr)
          if (ptr->is<int>())
            r.push_back(ptr->get<int>());
      //
      range = r;
    } else {
      throw std::runtime_error("invalid input range!");
    }

    ///
    std::string optionStr = "";
    auto options = has_input("options") ? get_input<DictObject>("options")
                                        : std::make_shared<DictObject>();
    for (auto const &[k, v] : options->lut) {
      if (auto ptr = dynamic_cast<StringObject *>(v.get()); ptr != nullptr)
        optionStr += " " + k + " " + ptr->get();
    }
    if (verbose)
      fmt::print("option str: {}\n", optionStr);

    auto args = has_input("arguments") ? get_input<ListObject>("arguments")
                                       : std::make_shared<ListObject>();
    std::vector<const char *> as;
    for (auto &&arg : args->get())
      if (auto ptr = dynamic_cast<StringObject *>(arg.get()); ptr != nullptr)
        as.push_back(ptr->get().c_str());

    ///
    auto cmdScripts = detail::python_evaluate(cmdFmtStr, range, as, optionStr);
    if (verbose)
      fmt::print("Captured python evaluation: [\n{}\n]\n", cmdScripts);

    ///
    auto deps = has_input("dependencies")
                    ? get_input<ListObject>("dependencies")
                    : std::make_shared<ListObject>();

    /// store in descriptor
    auto ret = std::make_shared<WorkNode>();
    ret->tag = tag;
    std::istringstream iss(cmdScripts);
    std::string line;
    while (std::getline(iss, line))
      ret->workItems.push_back(line);
    for (auto &&arg : deps->get())
      if (auto ptr = std::dynamic_pointer_cast<WorkNode>(arg); ptr)
        ret->deps[ptr->tag] = ptr;

    auto cmds = std::make_shared<ListObject>();
    for (auto &&item : ret->workItems)
      cmds->arr.push_back(std::make_shared<StringObject>(item));
    set_output("cmd_scripts", cmds);
    set_output("job", ret);
  }
};

ZENO_DEFNODE(CommandGenerator)
({/* inputs: */
  {
      {"string", "name_tag", "job0"},
      {"int", "range", "1"}, // int, int list, int range
      // {"int", "batch_size", "1"},

      {"string", "cmd_fmt_string", "cmd {range}"},
      {"list", "arguments"},
      {"DictObject", "options"},

      {"DictObject", "attributes"},
      {"list", "dependencies"},
      {"bool", "verbose", "false"},
  },
  /* outputs: */
  {
      {"list", "cmd_scripts"},
      {"WorkNode", "job"},
  },
  /* params: */
  {},
  /* category: */
  {
      "task",
  }});

struct CapturePyScriptOutput : INode {
  void apply() override {
    auto script = get_input2<std::string>("script");

    auto args = has_input("arguments") ? get_input<ListObject>("arguments")
                                       : std::make_shared<ListObject>();

    ///
    auto cmdScripts = detail::python_evaluate(script, args);

    set_output("output", std::make_shared<StringObject>(cmdScripts));
  }
};

ZENO_DEFNODE(CapturePyScriptOutput)
({/* inputs: */
  {
      {"multiline_string", "script",
       "import sys\r"
       "argc = len(sys.argv)\r"
       "print('argc: ', argc)\r"
       "for i in range(argc):\r"
       "	print('arg[', i, ']: ', sys.argv[i])\r"},
      {"list", "arguments"},
  },
  /* outputs: */
  {
      {"string", "output"},
  },
  /* params: */
  {},
  /* category: */
  {
      "task",
  }});

} // namespace zeno