# This file is included by the CMakeLists.txt in this directory.

AddDependency(NAME colpack
              GIT_URL https://github.com/chrisdembia/colpack.git
              GIT_TAG master
              CMAKE_ARGS -DCMAKE_DEBUG_POSTFIX:STRING=_d
                         -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON)

