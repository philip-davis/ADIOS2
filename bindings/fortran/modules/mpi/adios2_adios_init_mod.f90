!
! Distributed under the OSI-approved Apache License, Version 2.0.  See
!  accompanying file Copyright.txt for details.
!
!  adios2_adios_init_mod.f90 : ADIOS2 Fortran bindings for ADIOS class Init
!                              functions
!
!   Created on: Mar 13, 2017
!       Author: William F Godoy godoywf@ornl.gov
!

module adios2_adios_init

    use adios2_functions
    implicit none

contains

    subroutine adios2_init(adios, comm, adios2_debug_mode, ierr)
        integer(kind=8), intent(out) :: adios
        integer, intent(in) :: comm
        logical, value, intent(in) :: adios2_debug_mode
        integer, intent(out) :: ierr

        call adios2_init_config(adios, char(0), comm, adios2_debug_mode, ierr)

    end subroutine


    subroutine adios2_init_config(adios, config_file, comm, adios2_debug_mode, &
        & ierr)
        integer(kind=8), intent(out) :: adios
        character*(*), intent(in) :: config_file
        integer, intent(in) :: comm
        logical, value, intent(in) :: adios2_debug_mode
        integer, intent(out) :: ierr
        ! local
        integer debug_mode

        debug_mode = adios2_LogicalToInt(adios2_debug_mode)
        call adios2_init_config_f2c(adios, &
                                    TRIM(ADJUSTL(config_file))//char(0), &
                                    comm, debug_mode, ierr)

    end subroutine

end module
