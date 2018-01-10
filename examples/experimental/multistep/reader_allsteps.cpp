/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * reader.cpp
 *
 *  Created on: Feb 13, 2017
 *      Author: pnorbert
 */

#include <iostream>
#include <vector>

#include <adios2.h>
#ifdef ADIOS2_HAVE_MPI
#include <mpi.h>
#endif

template <class T>
T **Make2DArray(int nRows, int nCols)
{
    T **ptr = new T *[nRows];
    ptr[0] = new T[nRows * nCols];
    for (int i = 1; i < nRows; i++)
    {
        ptr[i] = ptr[i - 1] + nCols;
    }
    return ptr;
}

template <class T>
void Delete2DArray(T **ptr)
{
    delete[] ptr[0];
    delete[] ptr;
}

template <class T>
void Print1DArray(T *ptr, int nElems, std::string name)
{
    std::cout << name << " = { ";
    for (int i = 0; i < nElems; i++)
    {
        std::cout << ptr[i] << " ";
    }
    std::cout << "}\n";
}

template <class T>
void Print2DArray(T **ptr, int nRows, int nCols, std::string name)
{
    std::cout << name << " = { \n";
    for (int step = 0; step < nRows; step++)
    {
        std::cout << "    { ";
        for (int col = 0; col < nCols; col++)
        {
            std::cout << ptr[step][col] << " ";
        }
        std::cout << "}\n";
    }
    std::cout << "}\n";
}

int main(int argc, char *argv[])
{
    int rank = 0, nproc = 1;
#ifdef ADIOS2_HAVE_MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);
#endif
    const bool adiosDebug = true;

#ifdef ADIOS2_HAVE_MPI
    adios2::ADIOS adios(MPI_COMM_WORLD, adios2::DebugON);
#else
    adios2::ADIOS adios(adios2::DebugON);
#endif

    // Info variables from the file
    int Nwriters;
    int Nsteps;
    // Data variables from the file
    // 1. Global value, constant across processes, constant over time
    unsigned int Nx;
    // 2. Local value, varying across processes, constant over time
    std::vector<int> ProcessID;
    // 3. Global array, global dimensions, local dimensions and offsets are
    // constant over time
    // std::vector<double> GlobalArrayFixedDims;

    // 4. Local array, local dimensions are
    // constant over time (but different across processors here)
    std::vector<float> LocalArrayFixedDims;

    // 5. Global value, constant across processes, VARYING value over time
    std::vector<unsigned int> Nys;
    // 6. Local value, varying across processes, VARYING over time
    // Read as a 2D array, time as first dimension

    // 7. Global array, dimensions and offsets are VARYING over time
    std::vector<double> GlobalArray;
    // 8. Local array, dimensions and offsets are VARYING over time
    std::vector<float> IrregularArray;

    try
    {
        // Define method for engine creation
        // 1. Get method def from config file or define new one
        adios2::IO &bpReaderSettings = adios.DeclareIO("input");
        if (!bpReaderSettings.InConfigFile())
        {
            // if not defined by user, we can change the default settings
            bpReaderSettings.SetEngine(
                "ADIOS1"); // BPFile is the default engine
            // see only one step at a time
            bpReaderSettings.SetParameters({{"OpenAsFile", "yes"}});
        }

        // Create engine smart pointer due to polymorphism,
        // Default behavior
        // auto bpReader = adios.Open( "myNumbers.bp", "r" );
        // this would just open with a default transport, which is "BP"
        auto bpReader =
            bpReaderSettings.Open("myNumbers.bp", adios2::Mode::Read);

        if (!bpReader)
            throw std::ios_base::failure(
                "ERROR: failed to open ADIOS bpReader\n");

        /* Note: there is no global number of steps. Each variable has its
         * own
         * number of steps */

        adios2::Variable<int> *vNproc = bpReader->InquireVariable<int>("Nproc");
        Nwriters = vNproc->m_Data[0];
        if (rank == 0)
            std::cout << "# of writers = " << Nwriters << std::endl;

        /* NX */
        /* There is a single value written once.
         */
        // read a Global scalar which has a single value in a step
        adios2::Variable<unsigned int> *vNX =
            bpReader->InquireVariable<unsigned int>("NX");
        Nx = vNX->m_Data[0];
        // bpReader->Read<unsigned int>("NX", &Nx);
        if (rank == 0)
            std::cout << "NX = " << Nx << std::endl;

        /* NY */
        /* We can read all into a 1D array with a step selection.
           Steps are not automatically presented as an array dimension
           and read does not read it as array by default.
        */
        adios2::Variable<unsigned int> *vNY =
            bpReader->InquireVariable<unsigned int>("NY");
        Nys.resize(vNY->m_AvailableSteps); // number of steps available
        // make a StepSelection to select multiple steps. Args: From, #of
        // consecutive steps
        // ? How do we make a selection for an arbitrary list of steps ?
        vNY->SetStepSelection(0, vNY->m_AvailableSteps);
        bpReader->Read<unsigned int>(*vNY, Nys.data());
        if (rank == 0)
            Print1DArray(Nys.data(), Nys.size(), "NY");

        /* ProcessID */
        adios2::Variable<int> *vProcessID =
            bpReader->InquireVariable<int>("ProcessID");
        if (vProcessID->m_Shape[0] != Nwriters)
        {
            std::cout << "ERROR: Unexpected array size of ProcessID = "
                      << vProcessID->m_Shape[0] << " != # of writers"
                      << std::endl;
        }
        ProcessID.resize(vProcessID->m_Shape[0]);
        bpReader->Read<int>(*vProcessID, ProcessID.data());
        if (rank == 0)
            Print1DArray(ProcessID.data(), ProcessID.size(), "ProcessID");

        /* Nparts */
        // Nparts local scalar is presented as a 1D array of Nwriters
        // elements.
        // We can read all steps into a 2D array of nproc * Nwriters
        adios2::Variable<unsigned int> *vNparts =
            bpReader->InquireVariable<unsigned int>("Nparts");
        unsigned int **Nparts =
            Make2DArray<unsigned int>(vNparts->m_AvailableSteps, Nwriters);
        vNparts->SetStepSelection(0, vNparts->m_AvailableSteps);
        bpReader->Read<unsigned int>(*vNparts, Nparts[0]);
        if (rank == 0)
            Print2DArray(Nparts, vNparts->m_AvailableSteps, Nwriters, "Nparts");
        Delete2DArray(Nparts);
#ifdef ADIOS2_HAVE_MPI
        MPI_Barrier(MPI_COMM_WORLD);
#endif

        /*
         * GlobalArrayFixedDims
         */
        // inquiry about a variable, whose name we know
        adios2::Variable<double> *vGlobalArrayFixedDims =
            bpReader->InquireVariable<double>("GlobalArrayFixedDims");

        if (vGlobalArrayFixedDims == nullptr)
            throw std::ios_base::failure(
                "ERROR: failed to find variable "
                "'GlobalArrayFixedDims' in input file\n");

        // ? how do we know about the type? std::string varNice->m_Type
        std::size_t gdim =
            vGlobalArrayFixedDims->m_Shape[0]; // ?member var or member func?
        std::size_t count = gdim / nproc;
        std::size_t start = rank * count;
        if (rank == nproc - 1)
        {
            count = gdim - (count * (nproc - 1));
        }

        if (rank == 0)
            std::cout << "GlobalArrayFixedDims parallel read" << std::endl;

        double **GlobalArrayFixedDims =
            Make2DArray<double>(vGlobalArrayFixedDims->m_AvailableSteps, count);

        // Make a 1D selection to describe the local dimensions of the variable
        // we READ and its offsets in the global spaces
        vGlobalArrayFixedDims->SetSelection({start}, {count});
        vGlobalArrayFixedDims->SetStepSelection(
            0, vGlobalArrayFixedDims->m_AvailableSteps);
        bpReader->Read<double>(*vGlobalArrayFixedDims, GlobalArrayFixedDims[0]);
#ifdef ADIOS2_HAVE_MPI
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Status status;
        int token = 0;
        if (rank > 0)
            MPI_Recv(&token, 1, MPI_INT, rank - 1, 0, MPI_COMM_WORLD, &status);
#endif
        std::cout << "Rank " << rank << " read start = " << start
                  << " count = " << count << std::endl;
        Print2DArray(GlobalArrayFixedDims,
                     vGlobalArrayFixedDims->m_AvailableSteps, count,
                     "GlobalArrayFixedDims");
#ifdef ADIOS2_HAVE_MPI
        if (rank < nproc - 1)
            MPI_Send(&token, 1, MPI_INT, rank + 1, 0, MPI_COMM_WORLD);
        MPI_Barrier(MPI_COMM_WORLD);
#endif
        Delete2DArray(GlobalArrayFixedDims);

        /*
         * LocalArrayFixedDims
         */
        // inquiry about a variable, whose name we know
        adios2::Variable<float> *vLocalArrayFixedDims =
            bpReader->InquireVariable<float>("LocalArrayFixedDims");
        if (vLocalArrayFixedDims->m_Shape[0] != adios2::IrregularDim)
        {
            throw std::ios_base::failure(
                "Unexpected condition: LocalArrayFixedDims array's fast "
                "dimension is supposed to be adios2::IrregularDim indicating "
                "an "
                "Irregular array\n");
        }
        std::cout << "LocalArrayFixedDims is irregular. Cannot read this "
                     "variable yet...\n";
#ifdef ADIOS2_HAVE_MPI
        MPI_Barrier(MPI_COMM_WORLD);
#endif

        /*
         * LocalArrayFixedDimsJoined
         */
        // inquiry about a variable, whose name we know
        adios2::Variable<float> *vLocalArrayFixedDimsJoined =
            bpReader->InquireVariable<float>("LocalArrayFixedDimsJoined");
        float **LocalArrayFixedDimsJoined =
            Make2DArray<float>(vLocalArrayFixedDimsJoined->m_AvailableSteps,
                               vLocalArrayFixedDimsJoined->m_Shape[0]);

        // Make a 1D selection to describe the local dimensions of the variable
        // we READ and its offsets in the global spaces
        vLocalArrayFixedDimsJoined->SetSelection(
            {0}, {vLocalArrayFixedDimsJoined->m_Shape[0]});
        vLocalArrayFixedDimsJoined->SetStepSelection(
            0, vLocalArrayFixedDimsJoined->m_AvailableSteps);
        bpReader->Read<float>(*vLocalArrayFixedDimsJoined,
                              LocalArrayFixedDimsJoined[0]);
        if (rank == 0)
            Print2DArray(LocalArrayFixedDimsJoined,
                         vLocalArrayFixedDimsJoined->m_AvailableSteps,
                         vLocalArrayFixedDimsJoined->m_Shape[0],
                         "LocalArrayFixedDimsJoined");
        Delete2DArray(LocalArrayFixedDimsJoined);
#ifdef ADIOS2_HAVE_MPI
        MPI_Barrier(MPI_COMM_WORLD);
#endif

        /*
         * GlobalArray which changes size over time
         */
        // inquiry about a variable, whose name we know
        adios2::Variable<double> *vGlobalArray =
            bpReader->InquireVariable<double>("GlobalArray");
        std::cout << "GlobalArray [" << vGlobalArray->m_Shape[0] << "]";
        std::cout << " = Cannot read this variable yet...\n";
        if (vGlobalArray->m_Shape[0] != adios2::IrregularDim)
        {
            throw std::ios_base::failure(
                "Unexpected condition: GlobalArray array's  "
                "dimension is supposed to be adios2::IrregularDim indicating "
                "an "
                "Irregular array\n");
        }
#ifdef ADIOS2_HAVE_MPI
        MPI_Barrier(MPI_COMM_WORLD);
#endif

        // Close file/stream
        bpReader->Close();
    }
    catch (std::invalid_argument &e)
    {
        if (rank == 0)
        {
            std::cout << "Invalid argument exception, STOPPING PROGRAM\n";
            std::cout << e.what() << "\n";
        }
    }
    catch (std::ios_base::failure &e)
    {
        if (rank == 0)
        {
            std::cout << "System exception, STOPPING PROGRAM\n";
            std::cout << e.what() << "\n";
        }
    }
    catch (std::exception &e)
    {
        if (rank == 0)
        {
            std::cout << "Exception, STOPPING PROGRAM\n";
            std::cout << e.what() << "\n";
        }
    }

#ifdef ADIOS2_HAVE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
#endif
    return 0;
}
