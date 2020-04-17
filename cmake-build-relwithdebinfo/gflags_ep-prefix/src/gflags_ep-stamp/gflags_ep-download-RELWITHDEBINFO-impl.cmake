set(command "/Applications/CLion.app/Contents/bin/cmake/mac/bin/cmake;-P;/Users/ejeppinger/Documents/CMU/cmu-db/p3/terrier/cmake-build-relwithdebinfo/gflags_ep-prefix/src/gflags_ep-stamp/download-gflags_ep.cmake")

execute_process(COMMAND ${command} RESULT_VARIABLE result)
if(result)
  set(msg "Command failed (${result}):\n")
  foreach(arg IN LISTS command)
    set(msg "${msg} '${arg}'")
  endforeach()
  message(FATAL_ERROR "${msg}")
endif()
set(command "/Applications/CLion.app/Contents/bin/cmake/mac/bin/cmake;-P;/Users/ejeppinger/Documents/CMU/cmu-db/p3/terrier/cmake-build-relwithdebinfo/gflags_ep-prefix/src/gflags_ep-stamp/verify-gflags_ep.cmake")

execute_process(COMMAND ${command} RESULT_VARIABLE result)
if(result)
  set(msg "Command failed (${result}):\n")
  foreach(arg IN LISTS command)
    set(msg "${msg} '${arg}'")
  endforeach()
  message(FATAL_ERROR "${msg}")
endif()
set(command "/Applications/CLion.app/Contents/bin/cmake/mac/bin/cmake;-P;/Users/ejeppinger/Documents/CMU/cmu-db/p3/terrier/cmake-build-relwithdebinfo/gflags_ep-prefix/src/gflags_ep-stamp/extract-gflags_ep.cmake")

execute_process(COMMAND ${command} RESULT_VARIABLE result)
if(result)
  set(msg "Command failed (${result}):\n")
  foreach(arg IN LISTS command)
    set(msg "${msg} '${arg}'")
  endforeach()
  message(FATAL_ERROR "${msg}")
endif()