/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * TestBPLargeMetadata.cpp :
 *
 *  Created on: Aug 1, 2018
 *      Author: William F Godoy godoywf@ornl.gov
 */

#include <cstdint>
#include <cstring>

#include <iostream>
#include <stdexcept>

#include <adios2.h>

#include <gtest/gtest.h>

#include "../SmallTestData.h"

class BPLargeMetadata : public ::testing::Test
{
public:
    BPLargeMetadata() = default;

    SmallTestData m_TestData;
};

TEST_F(BPLargeMetadata, BPWrite1D_LargeMetadata)
{
    // Each process would write a 4x2 array and all processes would
    // form a 2D 4 * (NumberOfProcess * Nx) matrix where Nx is 2 here
    const std::string fname("BPWrite1D_LargeMetadata.bp");

    int mpiRank = 0, mpiSize = 1;

    const std::size_t Nx = 10;
    const std::size_t NSteps = 1;
    const std::size_t NVars = 1000;

#ifdef ADIOS2_HAVE_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &mpiRank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpiSize);
#endif

// Write test data using ADIOS2

#ifdef ADIOS2_HAVE_MPI
    adios2::ADIOS adios(MPI_COMM_WORLD, adios2::DebugON);
#else
    adios2::ADIOS adios(true);
#endif
    {
        adios2::IO io = adios.DeclareIO("TestIO");

        adios2::Dims shape{static_cast<size_t>(mpiSize * Nx)};
        adios2::Dims start{static_cast<size_t>(mpiRank * Nx)};
        adios2::Dims count{Nx};

        std::vector<adios2::Variable<float>> varsR32(NVars);
        std::vector<adios2::Variable<double>> varsR64(NVars);

        for (auto i = 0; i < NVars; ++i)
        {
            varsR32[i] =
                io.DefineVariable<float>("varR32_" + std::to_string(i), shape,
                                         start, count, adios2::ConstantDims);
            varsR64[i] =
                io.DefineVariable<double>("varR64_" + std::to_string(i), shape,
                                          start, count, adios2::ConstantDims);
        }

        adios2::Engine bpWriter = io.Open(fname, adios2::Mode::Write);

        for (auto step = 0; step < NSteps; ++step)
        {
            // Generate test data for each process uniquely
            SmallTestData currentTestData = generateNewSmallTestData(
                m_TestData, static_cast<int>(step), mpiRank, mpiSize);

            bpWriter.BeginStep();
            for (auto i = 0; i < NVars; ++i)
            {
                bpWriter.Put(varsR32[i], currentTestData.R32.data());
                bpWriter.Put(varsR64[i], currentTestData.R64.data());
            }
            bpWriter.EndStep();
        }
        bpWriter.Close();
    }
}

int main(int argc, char **argv)
{
#ifdef ADIOS2_HAVE_MPI
    MPI_Init(nullptr, nullptr);
#endif

    int result;
    ::testing::InitGoogleTest(&argc, argv);
    result = RUN_ALL_TESTS();

#ifdef ADIOS2_HAVE_MPI
    MPI_Finalize();
#endif

    return result;
}
