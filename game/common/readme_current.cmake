# Fails if a demo's README does not contain the "Settings:" part of the demo's --help golden file verbatim. Run as
#   cmake -DHELP=<help.golden> -DREADME=<README.md> -P readme_current.cmake
cmake_minimum_required(VERSION 3.20)
file(READ "${HELP}" help)
file(READ "${README}" readme)
string(FIND "${help}" "Settings:\n" at)
if(at LESS 0)
  message(FATAL_ERROR "${HELP} has no settings block")
endif()
string(SUBSTRING "${help}" ${at} -1 block)
string(FIND "${readme}" "${block}" found)
if(found LESS 0)
  message(FATAL_ERROR "${README} does not contain the settings block of --help; paste the block from ${HELP} under \"Settings options\"")
endif()
