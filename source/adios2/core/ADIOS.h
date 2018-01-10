/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * ADIOS.h : ADIOS library starting point, factory class for IO objects
 *  Created on: Oct 3, 2016
 *      Author: William F Godoy godoywf@ornl.gov
 */

#ifndef ADIOS2_CORE_ADIOS_H_
#define ADIOS2_CORE_ADIOS_H_

/// \cond EXCLUDE_FROM_DOXYGEN
#include <functional> //std::function
#include <map>
#include <memory> //std::shared_ptr
#include <string>
#include <vector>
/// \endcond

#include "adios2/ADIOSConfig.h"
#include "adios2/ADIOSMPICommOnly.h"
#include "adios2/ADIOSTypes.h"
#include "adios2/core/IO.h"
#include "adios2/core/Operator.h"

namespace adios2
{

/** @brief Point of entry class for an application.
 *         Serves as factory of IO class objects and Transforms */
class ADIOS
{
public:
    /** Passed from parallel constructor, MPI_Comm is a pointer itself. */
    MPI_Comm m_MPIComm;

    /**
     * @brief Constructor for MPI applications WITH a XML config file
     * @param configFile XML format (maybe support different formats in the
     * future?)
     * @param mpiComm MPI communicator from application
     * @param debugMode true: extra exception checks (recommended)
     */
    ADIOS(const std::string configFile, MPI_Comm mpiComm,
          const bool debugMode = true, const std::string hostLanguage = "C++");

    /**
     * @brief Constructor for non-MPI applications WITH a XML config file
     * @param configFile XML format (maybe support different formats in the
     * future?)
     * @param debugMode true: extra exception checks (recommended)
     */
    ADIOS(const std::string configFile, const bool debugMode = true,
          const std::string hostLanguage = "C++");

    /**
     * @brief Constructor for MPI apps WITHOUT a XML config file
     * @param mpiComm MPI communicator from application
     * @param debugMode true: extra exception checks (recommended)
     */
    ADIOS(MPI_Comm mpiComm, const bool debugMode = true,
          const std::string hostLanguage = "C++");

    /**
     *  @brief ADIOS no-MPI default empty constructor
     *  @param debugMode true: extra exception checks (recommended)
     */
    ADIOS(const bool debugMode = true, const std::string hostLanguage = "C++");

    /**
     * Delete copy constructor explicitly. Objects shouldn't be allowed to be
     * redefined. Use smart pointers if this is absolutely necessary.
     * @param adios reference to another adios object
     */
    ADIOS(const ADIOS &adios) = delete;

    ~ADIOS() = default;

    /**
     * Declares a new IO class object and returns a reference to that object.
     * @param ioName must be unique
     * @return reference to newly created IO object inside ADIOS, if IO is
     * declared it throws an exception
     */
    IO &DeclareIO(const std::string name);

    /**
     * Retrieve a reference to an existing IO object created with DeclareIO.
     * Follow the C++11 STL containers at function.
     * @param name of IO to look for
     * @return if IO exists returns a reference to existing IO object inside
     * ADIOS, otherwise throws an exception
     */
    IO &AtIO(const std::string name);

    /**
     * Declares a derived class of the Operator abstract class. If object is
     * defined in the user
     * config file, by name, it will be already created during the processing of
     * the config file. So this function returns a reference to that object.
     * @param name must be unique for each operator created with DefineOperator
     * @param type from derived class
     * @param parameters optional parameters
     * @return reference to Operator object
     */
    Operator &DefineOperator(const std::string name, const std::string type,
                             const Params &parameters = Params());

    /**
     * Signature for passing Callback functions as operators
     * @param name unique operator name
     * @param function callable function
     * @param parameters
     * @return reference to Operator object
     */
    template <class R, class... Args>
    Operator &DefineOperator(const std::string name,
                             const std::function<R(Args...)> &function,
                             const Params &parameters = Params());

    /**
     * Retrieve a reference pointer to an existing Operator object
     * created with DefineOperator.
     * @return if IO exists returns a reference to existing IO object inside
     * ADIOS, otherwise a nullptr
     */
    Operator *InquireOperator(const std::string name) noexcept;

private:
    /** XML File to be read containing configuration information */
    const std::string m_ConfigFile;

    /** if true will do more checks, exceptions, warnings, expect slower code */
    const bool m_DebugMode = true;

    /** Changed by language bindings in constructor */
    const std::string m_HostLanguage = "C++";

    /**
     * @brief List of IO class objects defined from either ADIOS
     * configuration file (XML) or the DeclareIO function explicitly.
     * Using map (binary tree) to preserve references returned by DeclareIO.
     * <pre>
     *     Key: unique method name
     *     Value: IO class object
     * </pre>
     */
    std::map<std::string, IO> m_IOs;

    /** operators created with DefineOperator */
    std::map<std::string, std::shared_ptr<Operator>> m_Operators;

    /** throws exception if m_MPIComm = MPI_COMM_NULL */
    void CheckMPI() const;

/** define CallBack1 */
#define declare_type(T)                                                        \
    Operator &DefineCallBack(                                                  \
        const std::string name,                                                \
        const std::function<void(const T *, const std::string,                 \
                                 const std::string, const std::string,         \
                                 const Dims &)> &function,                     \
        const Params &parameters);

    ADIOS2_FOREACH_TYPE_1ARG(declare_type)
#undef declare_type

    /** define CallBack2 */
    Operator &DefineCallBack(
        const std::string name,
        const std::function<void(void *, const std::string &,
                                 const std::string &, const std::string &,
                                 const Dims &)> &function,
        const Params &parameters);
};

} // end namespace adios2

#include "ADIOS.inl"

#endif /* ADIOS2_ADIOS_H_ */
