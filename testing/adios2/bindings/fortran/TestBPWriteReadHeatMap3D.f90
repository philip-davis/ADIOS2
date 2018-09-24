program TestBPWriteReadHeatMap3D
  use mpi
  use adios2

  implicit none

  integer(kind=8) :: sum_i1, sum_i2
  type(adios2_adios) :: adios
  type(adios2_io) :: ioPut, ioGet
  type(adios2_engine) :: bpWriter, bpReader
  type(adios2_variable), dimension(6) :: var_temperatures, var_temperaturesIn

  integer(kind=1), dimension(:, :, :), allocatable :: temperatures_i1, &
                                                      sel_temperatures_i1

  integer(kind=2), dimension(:, :, :), allocatable :: temperatures_i2, &
                                                      sel_temperatures_i2

  integer(kind=4), dimension(:, :, :), allocatable :: temperatures_i4, &
                                                      sel_temperatures_i4

  integer(kind=8), dimension(:, :, :), allocatable :: temperatures_i8, &
                                                      sel_temperatures_i8

  real(kind=4), dimension(:, :, :), allocatable :: temperatures_r4, &
                                                   sel_temperatures_r4

  real(kind=8), dimension(:, :, :), allocatable :: temperatures_r8, &
                                                   sel_temperatures_r8

  integer(kind=8), dimension(3) :: ishape, istart, icount
  integer(kind=8), dimension(3) :: sel_start, sel_count
  integer :: ierr, irank, isize
  integer :: in1, in2, in3
  integer :: i1, i2, i3

  call MPI_INIT(ierr)
  call MPI_COMM_RANK(MPI_COMM_WORLD, irank, ierr)
  call MPI_COMM_SIZE(MPI_COMM_WORLD, isize, ierr)

  in1 = 5
  in2 = 10
  in3 = 10

  icount = (/in1, in2, in3/)
  istart = (/0, 0, in3*irank/)
  ishape = (/in1, in2, in3*isize/)

  allocate (temperatures_i1(in1, in2, in3))
  allocate (temperatures_i2(in1, in2, in3))
  allocate (temperatures_i4(in1, in2, in3))
  allocate (temperatures_i8(in1, in2, in3))
  allocate (temperatures_r4(in1, in2, in3))
  allocate (temperatures_r8(in1, in2, in3))

  temperatures_i1 = 1
  temperatures_i2 = 1
  temperatures_i4 = 1
  temperatures_i8 = 1_8
  temperatures_r4 = 1.0
  temperatures_r8 = 1.0_8

  ! Start adios2 Writer
  call adios2_init(adios, MPI_COMM_WORLD, adios2_debug_mode_on, ierr)
  call adios2_declare_io(ioPut, adios, 'HeatMapWrite', ierr)

  call adios2_define_variable(var_temperatures(1), ioPut, &
                              'temperatures_i1', adios2_type_integer1, &
                              3, ishape, istart, icount, &
                              adios2_constant_dims, ierr)

  call adios2_define_variable(var_temperatures(2), ioPut, &
                              'temperatures_i2', adios2_type_integer2, &
                              3, ishape, istart, icount, &
                              adios2_constant_dims, ierr)

  call adios2_define_variable(var_temperatures(3), ioPut, &
                              'temperatures_i4', adios2_type_integer4, &
                              3, ishape, istart, icount, &
                              adios2_constant_dims, ierr)

  call adios2_define_variable(var_temperatures(4), ioPut, &
                              'temperatures_i8', adios2_type_integer8, &
                              3, ishape, istart, icount, &
                              adios2_constant_dims, ierr)

  call adios2_define_variable(var_temperatures(5), ioPut, &
                              'temperatures_r4', adios2_type_real, &
                              3, ishape, istart, icount, &
                              adios2_constant_dims, ierr)

  call adios2_define_variable(var_temperatures(6), ioPut, &
                              'temperatures_r8', adios2_type_dp, &
                              3, ishape, istart, icount, &
                              adios2_constant_dims, ierr)

  call adios2_open(bpWriter, ioPut, 'HeatMap3D_f.bp', adios2_mode_write, &
                   ierr)

  call adios2_put(bpWriter, var_temperatures(1), temperatures_i1, ierr)
  call adios2_put(bpWriter, var_temperatures(2), temperatures_i2, ierr)
  call adios2_put(bpWriter, var_temperatures(3), temperatures_i4, ierr)
  call adios2_put(bpWriter, var_temperatures(4), temperatures_i8, ierr)
  call adios2_put(bpWriter, var_temperatures(5), temperatures_r4, ierr)
  call adios2_put(bpWriter, var_temperatures(6), temperatures_r8, ierr)

  call adios2_close(bpWriter, ierr)

  if (allocated(temperatures_i1)) deallocate (temperatures_i1)
  if (allocated(temperatures_i2)) deallocate (temperatures_i2)
  if (allocated(temperatures_i4)) deallocate (temperatures_i4)
  if (allocated(temperatures_i8)) deallocate (temperatures_i8)
  if (allocated(temperatures_r4)) deallocate (temperatures_r4)
  if (allocated(temperatures_r8)) deallocate (temperatures_r8)

  ! Start adios2 Reader in rank 0
  if (irank == 0) then

    call adios2_declare_io(ioGet, adios, 'HeatMapRead', ierr)

    call adios2_open(bpReader, ioGet, 'HeatMap3D_f.bp', &
                     adios2_mode_read, MPI_COMM_SELF, ierr)

    call adios2_inquire_variable(var_temperaturesIn(1), ioGet, &
                                 'temperatures_i1', ierr)
    call adios2_inquire_variable(var_temperaturesIn(2), ioGet, &
                                 'temperatures_i2', ierr)
    call adios2_inquire_variable(var_temperaturesIn(3), ioGet, &
                                 'temperatures_i4', ierr)
    call adios2_inquire_variable(var_temperaturesIn(4), ioGet, &
                                 'temperatures_i8', ierr)
    call adios2_inquire_variable(var_temperaturesIn(5), ioGet, &
                                 'temperatures_r4', ierr)
    call adios2_inquire_variable(var_temperaturesIn(6), ioGet, &
                                 'temperatures_r8', ierr)

    sel_start = (/0, 0, 0/)
    sel_count = (/ishape(1), ishape(2), ishape(3)/)

    allocate (sel_temperatures_i1(ishape(1), ishape(2), ishape(3)))
    allocate (sel_temperatures_i2(ishape(1), ishape(2), ishape(3)))
    allocate (sel_temperatures_i4(ishape(1), ishape(2), ishape(3)))
    allocate (sel_temperatures_i8(ishape(1), ishape(2), ishape(3)))
    allocate (sel_temperatures_r4(ishape(1), ishape(2), ishape(3)))
    allocate (sel_temperatures_r8(ishape(1), ishape(2), ishape(3)))

    sel_temperatures_i1 = 0
    sel_temperatures_i2 = 0
    sel_temperatures_i4 = 0
    sel_temperatures_i8 = 0_8
    sel_temperatures_r4 = 0.0_4
    sel_temperatures_r8 = 0.0_8

    call adios2_set_selection(var_temperaturesIn(1), 3, sel_start, sel_count, &
                              ierr)
    call adios2_set_selection(var_temperaturesIn(2), 3, sel_start, sel_count, &
                              ierr)
    call adios2_set_selection(var_temperaturesIn(3), 3, sel_start, sel_count, &
                              ierr)
    call adios2_set_selection(var_temperaturesIn(4), 3, sel_start, sel_count, &
                              ierr)
    call adios2_set_selection(var_temperaturesIn(5), 3, sel_start, sel_count, &
                              ierr)
    call adios2_set_selection(var_temperaturesIn(6), 3, sel_start, sel_count, &
                              ierr)

    call adios2_get(bpReader, var_temperaturesIn(1), sel_temperatures_i1, ierr)
    call adios2_get(bpReader, var_temperaturesIn(2), sel_temperatures_i2, ierr)
    call adios2_get(bpReader, var_temperaturesIn(3), sel_temperatures_i4, ierr)
    call adios2_get(bpReader, var_temperaturesIn(4), sel_temperatures_i8, ierr)
    call adios2_get(bpReader, var_temperaturesIn(5), sel_temperatures_r4, ierr)
    call adios2_get(bpReader, var_temperaturesIn(6), sel_temperatures_r8, ierr)

    call adios2_close(bpReader, ierr)

    sum_i1 = 0
    sum_i2 = 0

    do i3 = 1, sel_count(3)
      do i2 = 1, sel_count(2)
        do i1 = 1, sel_count(1)
          sum_i1 = sum_i1 + sel_temperatures_i1(i1, i2, i3)
          sum_i2 = sum_i2 + sel_temperatures_i2(i1, i2, i3)
        end do
      end do
    end do

    if (sum_i1 /= 500*isize) stop 'Test failed integer*1'
    if (sum_i2 /= 500*isize) stop 'Test failed integer*2'
    if (sum(sel_temperatures_i4) /= 500*isize) stop 'Test failed integer*4'
    if (sum(sel_temperatures_i8) /= 500*isize) stop 'Test failed integer*8'
    if (sum(sel_temperatures_r4) /= 500*isize) stop 'Test failed real*4'
    if (sum(sel_temperatures_r8) /= 500*isize) stop 'Test failed real*8'

    if (allocated(sel_temperatures_i1)) deallocate (sel_temperatures_i1)
    if (allocated(sel_temperatures_i2)) deallocate (sel_temperatures_i2)
    if (allocated(sel_temperatures_i4)) deallocate (sel_temperatures_i4)
    if (allocated(sel_temperatures_i8)) deallocate (sel_temperatures_i8)
    if (allocated(sel_temperatures_r4)) deallocate (sel_temperatures_r4)
    if (allocated(sel_temperatures_r8)) deallocate (sel_temperatures_r8)

  end if

  call adios2_finalize(adios, ierr)
  call MPI_Finalize(ierr)

end program TestBPWriteReadHeatMap3D
