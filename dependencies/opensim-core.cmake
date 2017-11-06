# This file is included by the CMakeLists.txt in this directory.

AddDependency(NAME       opensim-core
              # StatesTrajectory::createFromStatesStorage() assembles the states.
              URL        https://github.com/opensim-org/opensim-core/archive/7513accaa13a9431cf37e2bbc35ee7111f50baa6.zip
              CMAKE_ARGS -DBUILD_API_EXAMPLES:BOOL=OFF
                         -DBUILD_TESTING:BOOL=OFF
                         -DSIMBODY_HOME:PATH=${CMAKE_INSTALL_PREFIX}/simbody
                         -DCMAKE_PREFIX_PATH:PATH=${CMAKE_INSTALL_PREFIX}/docopt)

if(SUPERBUILD_opensim-core)

    # OpenSim's dependencies.
    if(UNIX AND NOT APPLE)
        set(SIMBODY_GIT_TAG fix_Ipopt_symbols)
    else()
        set(SIMBODY_GIT_TAG fd5c03115038a7398ed5ac04169f801a2aa737f2)
    endif()

    AddDependency(NAME simbody
                  GIT_URL    https://github.com/simbody/simbody.git
                  GIT_TAG    ${SIMBODY_GIT_TAG}
                  CMAKE_ARGS -DBUILD_EXAMPLES:BOOL=OFF 
                             -DBUILD_TESTING:BOOL=OFF)

    AddDependency(NAME       docopt
                  GIT_URL    https://github.com/docopt/docopt.cpp.git
                  GIT_TAG    68b814282252d75d7c98d073e5958c5b1a964241
                  CMAKE_ARGS -DCMAKE_DEBUG_POSTFIX:STRING=_d)

    add_dependencies(opensim-core simbody docopt)
endif()

