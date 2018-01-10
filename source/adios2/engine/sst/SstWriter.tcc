/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * Sst.h
 *
 *  Created on: Aug 17, 2017
 *      Author: Greg Eisenhauer
 */

#ifndef ADIOS2_ENGINE_SST_SST_WRITER_TCC_
#define ADIOS2_ENGINE_SST_SST_WRITER_TCC_

#include "SstWriter.h"

#include "adios2/ADIOSMPI.h"
#include "adios2/helper/adiosFunctions.h" //GetType<T>

namespace adios2
{

template <class T>
void SstWriter::PutSyncCommon(Variable<T> &variable, const T *values)
{
    variable.SetData(values);

    // This part will go away, this is just to monitor variables per rank

    if (variable.m_Count.empty())
    {
        variable.m_Count = variable.m_Shape;
    }
    if (variable.m_Start.empty())
    {
        variable.m_Start.assign(variable.m_Count.size(), 0);
    }

    std::cout << "Variable " << variable.m_Name << "\n";
    std::cout << "Shape ID ";
    switch (variable.m_ShapeID)
    {
    case ShapeID::GlobalValue:
        std::cout << "GlobalValue : ";
        break;
    case ShapeID::GlobalArray:
        std::cout << "GlobalArray : ";
        break;
    case ShapeID::JoinedArray:
        std::cout << "JoinedArray : ";
        break;
    case ShapeID::LocalValue:
        std::cout << "LocalValue : ";
        break;
    case ShapeID::LocalArray:
        std::cout << "LocalArray : ";
        break;
    }
    std::cout << "putshape ";
    for (auto it = variable.m_Count.begin(); it != variable.m_Count.end(); ++it)
    {
        std::cout << ' ' << *it;
    }
    std::cout << '\n';
    std::cout << "varshape ";
    for (auto it = variable.m_Shape.begin(); it != variable.m_Shape.end(); ++it)
    {
        std::cout << ' ' << *it;
    }
    std::cout << '\n';
    std::cout << "offsets ";
    for (auto it = variable.m_Start.begin(); it != variable.m_Start.end(); ++it)
    {
        std::cout << ' ' << *it;
    }
    std::cout << '\n';
    if (m_FFSmarshal)
    {
        SstMarshal(m_Output, (void *)&variable, variable.m_Name.c_str(),
                   variable.m_Type.c_str(), variable.m_ElementSize,
                   variable.m_Shape.size(), variable.m_Shape.data(),
                   variable.m_Count.data(), variable.m_Start.data(), values);
    }
    else
    {
        // Do BP marshaling
    }
}

} // end namespace adios

#endif /* ADIOS2_ENGINE_SST_SST_WRITER_H_ */
