cmake_minimum_required(VERSION 3.16.3)

project(clio_migrator)

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
  message(FATAL_ERROR "GCC 11+ required for building clio_migrator")
endif()

option(VERBOSE "Verbose build" TRUE)
if(VERBOSE)
  set(CMAKE_VERBOSE_MAKEFILE TRUE)
  set(FETCHCONTENT_QUIET FALSE CACHE STRING "Verbose FetchContent()")
endif()

#c++20 removed std::result_of but boost 1.75 is still using it.
add_definitions(-DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1)

add_library(clio)
target_compile_features(clio PUBLIC cxx_std_20)
target_include_directories(clio PUBLIC src)

include(FetchContent)
include(ExternalProject)
include(CMake/settings.cmake)
include(CMake/ClioVersion.cmake)
include(CMake/deps/rippled.cmake)
include(CMake/deps/libfmt.cmake)
include(CMake/deps/Boost.cmake)
include(CMake/deps/cassandra.cmake)
include(CMake/deps/SourceLocation.cmake)

target_sources(clio PRIVATE
  ## Main
  src/main/impl/Build.cpp
  ## Backend
  src/backend/BackendInterface.cpp
  src/backend/CassandraBackend.cpp
  src/backend/SimpleCache.cpp
  ## ETL
  src/etl/ETLSource.cpp
  src/etl/ProbingETLSource.cpp
  src/etl/NFTHelpers.cpp
  src/etl/ReportingETL.cpp
  ## Subscriptions
  src/subscriptions/SubscriptionManager.cpp
  ## RPC
  src/rpc/Errors.cpp
  src/rpc/RPC.cpp
  src/rpc/RPCHelpers.cpp
  src/rpc/Counters.cpp
  src/rpc/WorkQueue.cpp
  ## NextGen RPC
  src/rpc/common/Specs.cpp
  src/rpc/common/Validators.cpp
  ## NextGen RPC handler
  src/rpc/ngHandlers/AccountChannels.cpp
  src/rpc/ngHandlers/AccountCurrencies.cpp
  src/rpc/ngHandlers/Tx.cpp
  src/rpc/ngHandlers/GatewayBalances.cpp
  src/rpc/ngHandlers/LedgerEntry.cpp
  ## RPC Methods
  # Account
  src/rpc/handlers/AccountChannels.cpp
  src/rpc/handlers/AccountCurrencies.cpp
  src/rpc/handlers/AccountInfo.cpp
  src/rpc/handlers/AccountLines.cpp
  src/rpc/handlers/AccountOffers.cpp
  src/rpc/handlers/AccountObjects.cpp
  src/rpc/handlers/GatewayBalances.cpp
  src/rpc/handlers/NoRippleCheck.cpp
  # NFT
  src/rpc/handlers/NFTHistory.cpp
  src/rpc/handlers/NFTInfo.cpp
  src/rpc/handlers/NFTOffers.cpp
  # Ledger
  src/rpc/handlers/Ledger.cpp
  src/rpc/handlers/LedgerData.cpp
  src/rpc/handlers/LedgerEntry.cpp
  src/rpc/handlers/LedgerRange.cpp
  # Transaction
  src/rpc/handlers/Tx.cpp
  src/rpc/handlers/TransactionEntry.cpp
  src/rpc/handlers/AccountTx.cpp
  # Dex
  src/rpc/handlers/BookChanges.cpp
  src/rpc/handlers/BookOffers.cpp
  # Payment Channel
  src/rpc/handlers/ChannelAuthorize.cpp
  src/rpc/handlers/ChannelVerify.cpp
  # Subscribe
  src/rpc/handlers/Subscribe.cpp
  # Server
  src/rpc/handlers/ServerInfo.cpp
  # Utilities
  src/rpc/handlers/Random.cpp
  src/config/Config.cpp
  src/log/Logger.cpp
  src/util/Taggable.cpp)

add_executable(clio_migrator src/main/main.cpp)
target_link_libraries(clio_migrator PUBLIC clio)

add_executable(clio_verifier src/main/verify.cpp)
target_link_libraries(clio_verifier PUBLIC clio)
