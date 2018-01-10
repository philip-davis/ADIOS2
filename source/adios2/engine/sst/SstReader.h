/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * SstReader.h
 *
 *  Created on: Aug 17, 2017
 *      Author: Greg Eisenhauer
 */

#ifndef ADIOS2_ENGINE_SST_SSTREADER_H_
#define ADIOS2_ENGINE_SST_SSTREADER_H_

#include <iostream> //std::cout << Needs to go

#include <mpi.h>

#include "adios2/toolkit/sst/sst.h"

#include "adios2/core/Engine.h"
#include "adios2/core/IO.h"

namespace adios2
{

class SstReader : public Engine
{

public:
    /**
     * Constructor for sst engine Reader
     * @param adios
     * @param name
     * @param accessMode
     * @param mpiComm
     * @param method
     * @param debugMode
     * @param nthreads
     */
    SstReader(IO &io, const std::string &name, const Mode mode,
              MPI_Comm mpiComm);

    virtual ~SstReader() = default;

    StepStatus BeginStep();
    StepStatus BeginStep(StepMode mode, const float timeoutSeconds = 0.f);

    void EndStep();
    void Close(const int transportIndex = -1);
    void PerformGets();

private:
    void Init();
    SstStream m_Input;

#define declare_type(T)                                                        \
    void DoGetSync(Variable<T> &, T *) final;                                  \
    void DoGetDeferred(Variable<T> &, T *) final;                              \
    void DoGetDeferred(Variable<T> &, T &) final;
    ADIOS2_FOREACH_TYPE_1ARG(declare_type)
#undef declare_type

    template <class T>
    void GetSyncCommon(Variable<T> &variable, T *data);

    template <class T>
    void GetDeferredCommon(Variable<T> &variable, T *data);
};

} // end namespace adios

#endif /* ADIOS2_ENGINE_SST_SSTREADER_H_ */
