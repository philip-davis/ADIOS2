add_library(sst
  dp/dp.c
  dp/dummy_dp.c
  dp/rdma_dp.c
  cp/cp.c
  cp/cp_common.c
  cp/ffs_marshal.c
)

add_library(sst "")
set_property(TARGET sst PROPERTY C_STANDARD 99)
target_include_directories(sst PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${EVPATH_INCLUDE_DIRS} ${MPI_C_INCLUDE_PATH})
target_link_libraries(sst evpath::evpath fabric ${MPI_C_LIBRARIES})

install(TARGETS sst EXPORT adios2Exports
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
