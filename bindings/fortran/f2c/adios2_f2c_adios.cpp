/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * adios2_f2c_adios.cpp
 *
 *  Created on: Nov 8, 2017
 *      Author: William F Godoy godoywf@ornl.gov
 */

#include "adios2_f2c_adios.h"

#include <iostream> //std::cerr
#include <stdexcept>

#ifdef ADIOS2_HAVE_MPI_F
void FC_GLOBAL(adios2_init_f2c,
               ADIOS2_INIT_F2C)(adios2_ADIOS **adios, MPI_Fint *comm,
                                const int *debug_mode, int *ierr)
{
    FC_GLOBAL(adios2_init_config_f2c, ADIOS2_INIT_CONFIG_F2C)
    (adios, "", comm, debug_mode, ierr);
}

void FC_GLOBAL(adios2_init_config_f2c,
               ADIOS2_INIT_CONFIG_F2C)(adios2_ADIOS **adios,
                                       const char *config_file, MPI_Fint *comm,
                                       const int *debug_mode, int *ierr)
{
    *ierr = 0;
    try
    {
        *adios = adios2_init_config_glue(
            config_file, MPI_Comm_f2c(*comm),
            static_cast<adios2_debug_mode>(*debug_mode), "Fortran");
    }
    catch (std::exception &e)
    {
        std::cerr << "ADIOS2 init_config: " << e.what() << "\n";
        *ierr = -1;
    }
}
#else
void FC_GLOBAL(adios2_init_f2c, ADIOS2_INIT_F2C)(adios2_ADIOS **adios,
                                                 const int *debug_mode,
                                                 int *ierr)
{
    FC_GLOBAL(adios2_init_config_f2c, ADIOS2_INIT_CONFIG_F2C)
    (adios, "", debug_mode, ierr);
}

void FC_GLOBAL(adios2_init_config_f2c,
               ADIOS2_INIT_CONFIG_F2C)(adios2_ADIOS **adios,
                                       const char *config_file,
                                       const int *debug_mode, int *ierr)
{
    *ierr = 0;
    try
    {
        *adios = adios2_init_config_nompi_glue(
            config_file, static_cast<adios2_debug_mode>(*debug_mode),
            "Fortran");
    }
    catch (std::exception &e)
    {
        std::cerr << "ADIOS2 init_config: " << e.what() << "\n";
        *ierr = -1;
    }
}
#endif

void FC_GLOBAL(adios2_declare_io_f2c,
               ADIOS2_DECLARE_IO_F2C)(adios2_IO **io, adios2_ADIOS **adios,
                                      const char *io_name, int *ierr)
{
    *ierr = 0;

    try
    {
        *io = adios2_declare_io(*adios, io_name);
    }
    catch (std::exception &e)
    {
        std::cerr << "ADIOS2 declare_io: " << e.what() << "\n";
        *ierr = -1;
    }
}

void FC_GLOBAL(adios2_finalize_f2c, ADIOS2_FINALIZE_F2C)(adios2_ADIOS **adios,
                                                         int *ierr)
{
    *ierr = 0;
    try
    {
        adios2_finalize(*adios);
    }
    catch (std::exception &e)
    {
        std::cerr << "ADIOS2 finalize: " << e.what() << "\n";
        *ierr = -1;
    }
}
