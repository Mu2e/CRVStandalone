#latest in the "good" github directory
#/cvmfs/mu2e.opensciencegrid.org/artexternals/cmake/v3_17_3/Linux64bit+3.10-2.17/bin/cmake -DGeant4_DIR=/cvmfs/mu2e.opensciencegrid.org/artexternals/geant4/v4_10_6_p02b/Linux64bit+3.10-2.17-e19-mt-prof/lib64/Geant4-10.6.2/ ../wls


#works
/cvmfs/mu2e.opensciencegrid.org/artexternals/cmake/v3_20_0/Linux64bit+3.10-2.17/bin/cmake -DGeant4_DIR=$GEANT4_FQ_DIR/lib64/Geant4-10.7.1/ ../wls


#doesn't work
#/grid/fermiapp/products/mu2e/artexternals/cmake/v2_8_12_2/Linux64bit+2.6-2.12/bin/cmake -DGeant4_DIR=/cvmfs/mu2e.opensciencegrid.org/artexternals/geant4/v4_10_2_p03e/Linux64bit+2.6-2.12-e15-prof/lib64/Geant4-10.2.3/ ../wls

# echo "CXX_DEFINES += " $1 >> CMakeFiles/wls.dir/flags.make

make

