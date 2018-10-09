!
! Distributed under the OSI-approved Apache License, Version 2.0.  See
!  accompanying file Copyright.txt for details.
!
!  adios2_stream_open_mod.f90 : ADIOS2 Fortran bindings for high-level Stream
!                               class
!   Created on: Feb 13, 2018
!       Author: William F Godoy godoywf@ornl.gov
!

module adios2_fopen_mod
    use adios2_parameters_mod
    implicit none

    interface adios2_fopen
        module procedure adios2_fopen_default
        module procedure adios2_fopen_config
    end interface

contains

    subroutine adios2_fopen_default(unit, name, mode, ierr)
        type(adios2_file), intent(out):: unit
        character*(*), intent(in) :: name
        character*(*), intent(in) :: mode
        integer, intent(out) :: ierr

        call adios2_fopen_f2c(unit%f2c, TRIM(ADJUSTL(name))//char(0), &
                              TRIM(ADJUSTL(mode))//char(0), ierr)
        if( ierr == 0 ) unit%valid = .true.

    end subroutine

    subroutine adios2_fopen_config(unit, name, mode, config_file, &
                                   io_in_config_file, ierr)
        type(adios2_file), intent(out):: unit
        character*(*), intent(in) :: name
        character*(*), intent(in) :: mode
        character*(*), intent(in) :: config_file
        character*(*), intent(in) :: io_in_config_file
        integer, intent(out) :: ierr

        call adios2_fopen_config_f2c(unit%f2c, TRIM(ADJUSTL(name))//char(0), &
                                     TRIM(ADJUSTL(mode))//char(0), &
                                     TRIM(ADJUSTL(config_file))//char(0), &
                                     TRIM(ADJUSTL(io_in_config_file))//char(0),&
                                     ierr)
        if( ierr == 0 ) unit%valid = .true.

    end subroutine

end module
