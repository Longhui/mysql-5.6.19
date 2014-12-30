cc=gcc
cxx=g++
build_type=RelWithDebInfo
build_dir=build
package=0
with_tokudb=1
jemalloc_src=$PWD/jemalloc
mysql_src=$PWD/mysql-5.6.19

while [ $# -gt 0 ]
do
  arg=$1
  shift
  if [[ $arg =~ --(.*)=(.*) ]]
  then
    eval ${BASH_REMATCH[1]}=${BASH_REMATCH[2]}
  else
    break
  fi
done

if [ ! -d $build_dir ]
then
  mkdir -p $build_dir
fi

pushd $build_dir
build_dir=$PWD
jemalloc_prefix=$PWD/jemalloc
mysql_prefix=$PWD/mysql
popd

function get_ncpus()
{
  if [ -f /proc/cpuinfo ]
  then
    grep bogomips /proc/cpuinfo | wc -l
  else
    echo 1
  fi
}

#makejobs=$(get_ncpus)
makejobs=1

function build_jemalloc()
{
  if [ -d $jemalloc_src ]
  then
    pushd $jemalloc_src
    CC=$cc
    if [ ! -x configure ]
    then
      chmod u+x configure
    fi
    if [ ! -x include/jemalloc/internal/size_classes.sh ]
    then
      chmod u+x include/jemalloc/internal/size_classes.sh
    fi
    ./configure --with-private-namespace=jemalloc_ --prefix=$jemalloc_prefix
    make install -j$makejobs
    popd
  else
    echo "jemalloc not exist"
    exit 1
  fi
}

function build_mysql_server()
{
  local jemalloc_lib=$jemalloc_prefix/lib
  export CC=$cc
  export CXX=$cxx
  if [ -d $mysql_src ]
  then
    pushd $mysql_src
    mkdir -p bld
    pushd bld
    if [ $package -eq 1 ]
    then
      cmake .. -DBUILD_CONFIG=mysql_release -DWITH_TOKUDB=1 -DCMAKE_BUILD_TYPE=$build_type -DWITH_EMBEDDED_SERVER=0 -DTOKU_JEMALLOC_LIBRARY="-Wl,--whole-archive $jemalloc_lib/libjemalloc.a -Wl,-no-whole-archive"
    else
      cmake .. -DCMAKE_INSTALL_PREFIX=$mysql_prefix -DWITH_TOKUDB=1 -DBUILD_CONFIG=mysql_release -DCMAKE_BUILD_TYPE=$build_type -DWITH_EMBEDDED_SERVER=0 -DTOKU_JEMALLOC_LIBRARY="-Wl,--whole-archive $jemalloc_lib/libjemalloc.a -Wl,-no-whole-archive"
    fi
    if [ $? != 0 ]
    then
      exit 1
    fi
    if [ $package -eq 1 ]
    then
      make package -j$makejobs
    else
      make install -j$makejobs
    fi
    popd
    popd
  else
    echo "source of mysql not exist, exit"
    exit 1
  fi
}

function make_release_package()
{
  if [ $package -eq 1 ]
  then
    pushd $mysql_src/bld
    cp -f *.tar.gz $build_dir
    popd
    pushd $build_dir
    tarball=$(ls *.tar.gz)
    echo "calcuate the md5..."
    md5sum $tarball > $tarball.md5
    popd
  fi
}

function build_mysql_server_without_tokudb()
{
  if [ -d $mysl_src ]
  then
    pushd $mysql_src
    if [ ! -d bld ]
    then
      mkdir bld
      pushd bld
      if [ $package -eq 1 ]
      then
        cmake .. -DBUILD_CONFIG=mysql_release -DCMAKE_BUILD_TYPE=$build_type -DWITH_EMBEDDED_SERVER=0
        make package -j$makejobs
        cp -f *.tar.gz $build_dir
      else
        cmake .. -DCMAKE_INSTALL_PREFIX=$mysql_prefix -DBUILD_CONFIG=mysql_release -DCMAKE_BUILD_TYPE=$build_type -DWITH_EMBEDDED_SERVER=0
        make install -j$makejobs
      fi
      popd
    fi
    popd
  else
    echo "mysql source not exist"
    exit 1
  fi
}

if [ $with_tokudb -eq 1 ]
then
  echo "build mysql with tokudb"
  build_jemalloc
  build_mysql_server
  make_release_package
else
  echo "build mysql without tokudb"
  build_mysql_server_without_tokudb 
fi
