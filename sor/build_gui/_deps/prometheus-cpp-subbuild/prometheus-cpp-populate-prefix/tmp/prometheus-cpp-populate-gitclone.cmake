
if(NOT "/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-subbuild/prometheus-cpp-populate-prefix/src/prometheus-cpp-populate-stamp/prometheus-cpp-populate-gitinfo.txt" IS_NEWER_THAN "/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-subbuild/prometheus-cpp-populate-prefix/src/prometheus-cpp-populate-stamp/prometheus-cpp-populate-gitclone-lastrun.txt")
  message(STATUS "Avoiding repeated git clone, stamp file is up to date: '/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-subbuild/prometheus-cpp-populate-prefix/src/prometheus-cpp-populate-stamp/prometheus-cpp-populate-gitclone-lastrun.txt'")
  return()
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E rm -rf "/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-src"
  RESULT_VARIABLE error_code
  )
if(error_code)
  message(FATAL_ERROR "Failed to remove directory: '/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-src'")
endif()

# try the clone 3 times in case there is an odd git clone issue
set(error_code 1)
set(number_of_tries 0)
while(error_code AND number_of_tries LESS 3)
  execute_process(
    COMMAND "/usr/bin/git"  clone --no-checkout --depth 1 --no-single-branch --config "advice.detachedHead=false" "https://github.com/jupp0r/prometheus-cpp.git" "prometheus-cpp-src"
    WORKING_DIRECTORY "/home/saad/personal/klett/sor/build_gui/_deps"
    RESULT_VARIABLE error_code
    )
  math(EXPR number_of_tries "${number_of_tries} + 1")
endwhile()
if(number_of_tries GREATER 1)
  message(STATUS "Had to git clone more than once:
          ${number_of_tries} times.")
endif()
if(error_code)
  message(FATAL_ERROR "Failed to clone repository: 'https://github.com/jupp0r/prometheus-cpp.git'")
endif()

execute_process(
  COMMAND "/usr/bin/git"  checkout v1.2.4 --
  WORKING_DIRECTORY "/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-src"
  RESULT_VARIABLE error_code
  )
if(error_code)
  message(FATAL_ERROR "Failed to checkout tag: 'v1.2.4'")
endif()

set(init_submodules TRUE)
if(init_submodules)
  execute_process(
    COMMAND "/usr/bin/git"  submodule update --recursive --init 
    WORKING_DIRECTORY "/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-src"
    RESULT_VARIABLE error_code
    )
endif()
if(error_code)
  message(FATAL_ERROR "Failed to update submodules in: '/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-src'")
endif()

# Complete success, update the script-last-run stamp file:
#
execute_process(
  COMMAND ${CMAKE_COMMAND} -E copy
    "/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-subbuild/prometheus-cpp-populate-prefix/src/prometheus-cpp-populate-stamp/prometheus-cpp-populate-gitinfo.txt"
    "/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-subbuild/prometheus-cpp-populate-prefix/src/prometheus-cpp-populate-stamp/prometheus-cpp-populate-gitclone-lastrun.txt"
  RESULT_VARIABLE error_code
  )
if(error_code)
  message(FATAL_ERROR "Failed to copy script-last-run stamp file: '/home/saad/personal/klett/sor/build_gui/_deps/prometheus-cpp-subbuild/prometheus-cpp-populate-prefix/src/prometheus-cpp-populate-stamp/prometheus-cpp-populate-gitclone-lastrun.txt'")
endif()

