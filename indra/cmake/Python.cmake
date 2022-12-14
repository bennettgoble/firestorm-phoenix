# -*- cmake -*-

set(PYTHONINTERP_FOUND)

if (WINDOWS)
  # On Windows, explicitly avoid Cygwin Python.

  if (DEFINED ENV{VIRTUAL_ENV})
    find_program(PYTHON_EXECUTABLE
      NAMES python.exe
      PATHS
      "$ENV{VIRTUAL_ENV}\\scripts"
      NO_DEFAULT_PATH
      )
  else()
    set(Python3_FIND_REGISTRY "LAST")
    find_program(PYTHON_EXECUTABLE
      NAMES python.exe
      NO_DEFAULT_PATH # added so that cmake does not find cygwin python
      PATHS
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.9\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.10\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.11\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\3.9\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\3.10\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.11\\InstallPath]
      )
  endif()
    include(FindPythonInterp)
else()
  find_program(PYTHON_EXECUTABLE python3)

  if (PYTHON_EXECUTABLE)
    set(PYTHONINTERP_FOUND ON)
  endif (PYTHON_EXECUTABLE)
endif (WINDOWS)

if (NOT PYTHON_EXECUTABLE)
  message(FATAL_ERROR "No Python interpreter found")
endif (NOT PYTHON_EXECUTABLE)

mark_as_advanced(PYTHON_EXECUTABLE)
