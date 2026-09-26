pybind11_add_module(_khseg MODULE bindings.cpp)
target_link_libraries(_khseg PRIVATE khseg::khseg)
target_compile_features(_khseg PRIVATE cxx_std_20)
set_target_properties(_khseg PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/khseg)
if(MINGW)
  # The CPython on Windows is built with MSVC; link the GCC runtime statically
  # so the module does not need MinGW DLLs next to it.
  target_link_options(_khseg PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# Copy the pure-Python part next to the built module so the build tree can be
# imported directly (PYTHONPATH=<build>/python) for tests.
configure_file(khseg/__init__.py ${CMAKE_CURRENT_BINARY_DIR}/khseg/__init__.py COPYONLY)
configure_file(khseg/__init__.pyi ${CMAKE_CURRENT_BINARY_DIR}/khseg/__init__.pyi COPYONLY)
configure_file(khseg/py.typed ${CMAKE_CURRENT_BINARY_DIR}/khseg/py.typed COPYONLY)

if(SKBUILD)
  install(TARGETS _khseg LIBRARY DESTINATION khseg)
endif()

if(KHSEG_BUILD_TESTS)
  add_test(NAME python.unittest
    COMMAND ${Python_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -v)
  set_tests_properties(python.unittest PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR};PYTHONIOENCODING=utf-8")
endif()
