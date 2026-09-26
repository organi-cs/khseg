#include <CLI/CLI.hpp>
#include <khseg/khseg.hpp>

#include <iostream>

int main(int argc, char** argv) {
  CLI::App app{"khseg: Khmer word segmenter"};
  app.set_version_flag("--version", std::string("khseg ") + khseg::version());
  CLI11_PARSE(app, argc, argv);
  return 0;
}
