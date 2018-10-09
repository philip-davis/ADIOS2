/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * adios2_c_io.cpp
 *
 *  Created on: Nov 8, 2017
 *      Author: William F Godoy godoywf@ornl.gov
 */

#include "adios2_c_io.h"

#include <vector>

#include "adios2/ADIOSMPI.h"
#include "adios2/core/IO.h"
#include "adios2/helper/adiosFunctions.h" //GetType<T>

#ifdef __cplusplus
extern "C" {
#endif

adios2_error adios2_in_config_file(adios2_bool *result, const adios2_io *io)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_in_config_file");
        *result = reinterpret_cast<const adios2::core::IO *>(io)->InConfigFile()
                      ? adios2_true
                      : adios2_false;
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_in_config_file"));
    }
}

adios2_error adios2_set_engine(adios2_io *io, const char *engine_type)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_set_engine");
        reinterpret_cast<adios2::core::IO *>(io)->SetEngine(engine_type);
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_set_engine"));
    }
}

adios2_error adios2_set_parameter(adios2_io *io, const char *key,
                                  const char *value)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_set_parameter");
        reinterpret_cast<adios2::core::IO *>(io)->SetParameter(key, value);
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_set_parameter"));
    }
}

adios2_error adios2_add_transport(size_t *transport_index, adios2_io *io,
                                  const char *type)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_add_transport");
        *transport_index =
            reinterpret_cast<adios2::core::IO *>(io)->AddTransport(type);
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_add_transport"));
    }
}

adios2_error adios2_set_transport_parameter(adios2_io *io,
                                            const size_t transport_index,
                                            const char *key, const char *value)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_set_transport_parameter");
        reinterpret_cast<adios2::core::IO *>(io)->SetTransportParameter(
            transport_index, key, value);
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_set_transport_parameter"));
    }
}

adios2_variable *
adios2_define_variable(adios2_io *io, const char *name, const adios2_type type,
                       const size_t ndims, const size_t *shape,
                       const size_t *start, const size_t *count,
                       const adios2_constant_dims constant_dims)
{
    adios2_variable *variable = nullptr;

    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_define_variable");

        adios2::core::VariableBase *variableCpp = nullptr;
        adios2::core::IO &ioCpp = *reinterpret_cast<adios2::core::IO *>(io);
        const bool constantSizeBool =
            (constant_dims == adios2_constant_dims_true) ? true : false;

        auto lf_ConvertDims = [](const size_t ndims,
                                 const size_t *in) -> adios2::Dims {

            if (in != nullptr)
            {
                return adios2::Dims(in, in + ndims);
            }
            return adios2::Dims();
        };

        const adios2::Dims shapeV = lf_ConvertDims(ndims, shape);
        const adios2::Dims startV = lf_ConvertDims(ndims, start);
        const adios2::Dims countV = lf_ConvertDims(ndims, count);

        switch (type)
        {

        case (adios2_type_string):
        {
            variableCpp = &ioCpp.DefineVariable<std::string>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_char):
        {
            variableCpp = &ioCpp.DefineVariable<char>(name, shapeV, startV,
                                                      countV, constantSizeBool);
            break;
        }
        case (adios2_type_signed_char):
        {
            variableCpp = &ioCpp.DefineVariable<signed char>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_short):
        {
            variableCpp = &ioCpp.DefineVariable<short>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_int):
        {
            variableCpp = &ioCpp.DefineVariable<int>(name, shapeV, startV,
                                                     countV, constantSizeBool);
            break;
        }
        case (adios2_type_long_int):
        {
            variableCpp = &ioCpp.DefineVariable<long int>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_long_long_int):
        {
            variableCpp = &ioCpp.DefineVariable<long long int>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_unsigned_char):
        {
            variableCpp = &ioCpp.DefineVariable<unsigned char>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_unsigned_short):
        {
            variableCpp = &ioCpp.DefineVariable<unsigned short>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_unsigned_int):
        {
            variableCpp = &ioCpp.DefineVariable<unsigned int>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_unsigned_long_int):
        {
            variableCpp = &ioCpp.DefineVariable<unsigned long int>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_unsigned_long_long_int):
        {
            variableCpp = &ioCpp.DefineVariable<unsigned long long int>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_float):
        {
            variableCpp = &ioCpp.DefineVariable<float>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_double):
        {
            variableCpp = &ioCpp.DefineVariable<double>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_float_complex):
        {
            variableCpp = &ioCpp.DefineVariable<std::complex<float>>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_double_complex):
        {
            variableCpp = &ioCpp.DefineVariable<std::complex<double>>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_int8_t):
        {
            variableCpp = &ioCpp.DefineVariable<int8_t>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_int16_t):
        {
            variableCpp = &ioCpp.DefineVariable<int16_t>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_int32_t):
        {
            variableCpp = &ioCpp.DefineVariable<int32_t>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_int64_t):
        {
            variableCpp = &ioCpp.DefineVariable<int64_t>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_uint8_t):
        {
            variableCpp = &ioCpp.DefineVariable<uint8_t>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_uint16_t):
        {
            variableCpp = &ioCpp.DefineVariable<uint16_t>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_uint32_t):
        {
            variableCpp = &ioCpp.DefineVariable<uint32_t>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        case (adios2_type_uint64_t):
        {
            variableCpp = &ioCpp.DefineVariable<uint64_t>(
                name, shapeV, startV, countV, constantSizeBool);
            break;
        }
        default:
        {
            throw std::invalid_argument("ERROR: unsupported type " +
                                        std::to_string(static_cast<int>(type)) +
                                        ", see enum adios2_type for acceptable "
                                        "types, in call to "
                                        "adios2_define_variable\n");
        }
        }

        variable = reinterpret_cast<adios2_variable *>(variableCpp);
    }
    catch (...)
    {

        adios2::helper::ExceptionToError("adios2_define_variable");
    }
    return variable;
}

adios2_variable *adios2_inquire_variable(adios2_io *io, const char *name)
{
    adios2_variable *variable = nullptr;

    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_inquire_variable");

        adios2::core::IO &ioCpp = *reinterpret_cast<adios2::core::IO *>(io);
        const auto &dataMap = ioCpp.GetVariablesDataMap();

        auto itVariable = dataMap.find(name);
        if (itVariable == dataMap.end()) // not found
        {
            return variable;
        }

        const std::string type(ioCpp.InquireVariableType(name));
        adios2::core::VariableBase *variableCpp = nullptr;

        if (type == "compound")
        {
            // not supported
        }
#define declare_template_instantiation(T)                                      \
    else if (type == adios2::helper::GetType<T>())                             \
    {                                                                          \
        variableCpp = ioCpp.InquireVariable<T>(name);                          \
    }
        ADIOS2_FOREACH_TYPE_1ARG(declare_template_instantiation)
#undef declare_template_instantiation

        variable = reinterpret_cast<adios2_variable *>(variableCpp);
    }
    catch (...)
    {

        adios2::helper::ExceptionToError("adios2_inquire_variable");
    }
    return variable;
}

adios2_error adios2_inquire_all_variables(adios2_variable ***variables,
                                          size_t *size, adios2_io *io)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_inquire_all_variables");

        adios2::core::IO &ioCpp = *reinterpret_cast<adios2::core::IO *>(io);
        const auto &dataMap = ioCpp.GetVariablesDataMap();

        *size = dataMap.size();
        adios2_variable **list =
            (adios2_variable **)calloc(*size, sizeof(adios2_variable *));
        size_t n = 0;
        for (auto it = dataMap.begin(); it != dataMap.end(); ++it)
        {
            const std::string name(it->first);
            const std::string type(it->second.first);
            adios2::core::VariableBase *variable = nullptr;

            if (type == "compound")
            {
                // not supported
            }
#define declare_template_instantiation(T)                                      \
    else if (type == adios2::helper::GetType<T>())                             \
    {                                                                          \
        variable = ioCpp.InquireVariable<T>(name);                             \
        list[n] = reinterpret_cast<adios2_variable *>(variable);               \
    }
            ADIOS2_FOREACH_TYPE_1ARG(declare_template_instantiation)
#undef declare_template_instantiation

            n++;
        }
        *variables = list;
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_inquire_all_variables"));
    }
}

adios2_attribute *adios2_define_attribute(adios2_io *io, const char *name,
                                          const adios2_type type,
                                          const void *data, const size_t size)
{
    return adios2_define_variable_attribute(io, name, type, data, size, "", "");
}

adios2_attribute *adios2_define_variable_attribute(
    adios2_io *io, const char *name, const adios2_type type, const void *data,
    const size_t elements, const char *variable_name, const char *separator)
{
    adios2_attribute *attribute = nullptr;

    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_define_attribute");

        adios2::helper::CheckForNullptr(
            data, "for const void* data, in call to adios2_define_attribute");

        adios2::core::IO &ioCpp = *reinterpret_cast<adios2::core::IO *>(io);
        adios2::core::AttributeBase *attributeCpp = nullptr;

        switch (type)
        {

        case (adios2_type_string):
        {
            // relying on null terminated character for the size
            const std::string singleString(
                reinterpret_cast<const char *>(data));
            attributeCpp = &ioCpp.DefineAttribute<std::string>(
                name, singleString, variable_name, separator);
            break;
        }
        case (adios2_type_string_array):
        {
            const char **char2D =
                reinterpret_cast<const char **>(const_cast<void *>(data));

            std::vector<std::string> arrayStrings(char2D, char2D + elements);

            attributeCpp = &ioCpp.DefineAttribute<std::string>(
                name, arrayStrings.data(), arrayStrings.size(), variable_name,
                separator);
            break;
        }
        case (adios2_type_char):
        {
            attributeCpp = &ioCpp.DefineAttribute<char>(
                name, reinterpret_cast<const char *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_signed_char):
        {
            attributeCpp = &ioCpp.DefineAttribute<signed char>(
                name, reinterpret_cast<const signed char *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_short):
        {
            attributeCpp = &ioCpp.DefineAttribute<short>(
                name, reinterpret_cast<const short *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_int):
        {
            attributeCpp = &ioCpp.DefineAttribute<int>(
                name, reinterpret_cast<const int *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_long_int):
        {
            attributeCpp = &ioCpp.DefineAttribute<long int>(
                name, reinterpret_cast<const long int *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_long_long_int):
        {
            attributeCpp = &ioCpp.DefineAttribute<long long int>(
                name, reinterpret_cast<const long long int *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_unsigned_char):
        {
            attributeCpp = &ioCpp.DefineAttribute<unsigned char>(
                name, reinterpret_cast<const unsigned char *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_unsigned_short):
        {
            attributeCpp = &ioCpp.DefineAttribute<unsigned short>(
                name, reinterpret_cast<const unsigned short *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_unsigned_int):
        {
            attributeCpp = &ioCpp.DefineAttribute<unsigned int>(
                name, reinterpret_cast<const unsigned int *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_unsigned_long_int):
        {
            attributeCpp = &ioCpp.DefineAttribute<unsigned long int>(
                name, reinterpret_cast<const unsigned long int *>(data),
                elements, variable_name, separator);
            break;
        }
        case (adios2_type_unsigned_long_long_int):
        {
            attributeCpp = &ioCpp.DefineAttribute<unsigned long long int>(
                name, reinterpret_cast<const unsigned long long int *>(data),
                elements, variable_name, separator);
            break;
        }
        case (adios2_type_float):
        {
            attributeCpp = &ioCpp.DefineAttribute<float>(
                name, reinterpret_cast<const float *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_double):
        {
            attributeCpp = &ioCpp.DefineAttribute<double>(
                name, reinterpret_cast<const double *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_int8_t):
        {
            attributeCpp = &ioCpp.DefineAttribute<int8_t>(
                name, reinterpret_cast<const int8_t *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_int16_t):
        {
            attributeCpp = &ioCpp.DefineAttribute<int16_t>(
                name, reinterpret_cast<const int16_t *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_int32_t):
        {
            attributeCpp = &ioCpp.DefineAttribute<int32_t>(
                name, reinterpret_cast<const int32_t *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_int64_t):
        {
            attributeCpp = &ioCpp.DefineAttribute<int64_t>(
                name, reinterpret_cast<const int64_t *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_uint8_t):
        {
            attributeCpp = &ioCpp.DefineAttribute<uint8_t>(
                name, reinterpret_cast<const uint8_t *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_uint16_t):
        {
            attributeCpp = &ioCpp.DefineAttribute<uint16_t>(
                name, reinterpret_cast<const uint16_t *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_uint32_t):
        {
            attributeCpp = &ioCpp.DefineAttribute<uint32_t>(
                name, reinterpret_cast<const uint32_t *>(data), elements,
                variable_name, separator);
            break;
        }
        case (adios2_type_uint64_t):
        {
            attributeCpp = &ioCpp.DefineAttribute<uint64_t>(
                name, reinterpret_cast<const uint64_t *>(data), elements,
                variable_name, separator);
            break;
        }
        default:
        {
            throw std::invalid_argument("ERROR: unsupported type " +
                                        std::to_string(static_cast<int>(type)) +
                                        ", see enum adios2_type for acceptable "
                                        "types, in call to "
                                        "adios2_define_attribute\n");
        }
        }

        attribute = reinterpret_cast<adios2_attribute *>(attributeCpp);
    }
    catch (...)
    {
        adios2::helper::ExceptionToError(
            "adios2_define_attribute or adios_define_variable_attribute");
    }
    return attribute;
}

adios2_attribute *adios2_inquire_attribute(adios2_io *io, const char *name)
{
    adios2_attribute *attribute = nullptr;
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_inquire_attribute");

        adios2::core::IO &ioCpp = *reinterpret_cast<adios2::core::IO *>(io);
        const auto &dataMap = ioCpp.GetAttributesDataMap();

        auto itAttribute = dataMap.find(name);
        if (itAttribute == dataMap.end()) // not found
        {
            return attribute;
        }

        const std::string type(itAttribute->second.first);
        adios2::core::AttributeBase *attributeCpp = nullptr;

        if (type == "compound")
        {
            // not supported
        }
#define declare_template_instantiation(T)                                      \
    else if (type == adios2::helper::GetType<T>())                             \
    {                                                                          \
        attributeCpp = ioCpp.InquireAttribute<T>(name);                        \
    }
        ADIOS2_FOREACH_ATTRIBUTE_TYPE_1ARG(declare_template_instantiation)
#undef declare_template_instantiation

        attribute = reinterpret_cast<adios2_attribute *>(attributeCpp);
    }
    catch (...)
    {
        adios2::helper::ExceptionToError(
            "adios2_inquire_attribute or adios_inquire_variable_attribute");
    }
    return attribute;
}

adios2_attribute *adios2_inquire_variable_attribute(adios2_io *io,
                                                    const char *name,
                                                    const char *variable_name,
                                                    const char *separator)
{
    // TODO check const char* are not null
    const std::string globalName =
        std::string(variable_name) + std::string(separator) + std::string(name);
    return adios2_inquire_attribute(io, globalName.c_str());
}

adios2_error adios2_inquire_all_attributes(adios2_attribute ***attributes,
                                           size_t *size, adios2_io *io)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_inquire_all_attributes");

        adios2::core::IO &ioCpp = *reinterpret_cast<adios2::core::IO *>(io);
        const auto &dataMap = ioCpp.GetAttributesDataMap();

        *size = dataMap.size();
        adios2_attribute **list =
            (adios2_attribute **)calloc(*size, sizeof(adios2_attribute *));
        size_t n = 0;
        for (auto it = dataMap.begin(); it != dataMap.end(); ++it)
        {
            const std::string name(it->first);
            const std::string type(it->second.first);
            adios2::core::AttributeBase *attribute = nullptr;

            if (type == "compound")
            {
                // not supported
            }
#define declare_template_instantiation(T)                                      \
    else if (type == adios2::helper::GetType<T>())                             \
    {                                                                          \
        attribute = ioCpp.InquireAttribute<T>(name);                           \
        list[n] = reinterpret_cast<adios2_attribute *>(attribute);             \
    }
            ADIOS2_FOREACH_ATTRIBUTE_TYPE_1ARG(declare_template_instantiation)
#undef declare_template_instantiation

            n++;
        }
        *attributes = list;
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_inquire_all_attributes"));
    }
}

// DANGEROUS
adios2_error adios2_remove_variable(adios2_io *io, const char *name,
                                    adios2_bool *result)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_remove_variable");

        *result = reinterpret_cast<adios2::core::IO *>(io)->RemoveVariable(name)
                      ? adios2_true
                      : adios2_false;

        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_remove_variable"));
    }
}

adios2_error adios2_remove_all_variables(adios2_io *io)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_remove_all_variables");

        reinterpret_cast<adios2::core::IO *>(io)->RemoveAllVariables();
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_remove_all_variables"));
    }
}

adios2_error adios2_remove_attribute(adios2_io *io, const char *name,
                                     adios2_bool *result)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_remove_attribute");
        *result =
            reinterpret_cast<adios2::core::IO *>(io)->RemoveAttribute(name)
                ? adios2_true
                : adios2_false;
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_remove_attribute"));
    }
}

adios2_error adios2_remove_all_attributes(adios2_io *io)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_remove_all_attributes");
        reinterpret_cast<adios2::core::IO *>(io)->RemoveAllAttributes();
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_remove_attribute"));
    }
}

namespace
{

adios2::Mode adios2_ToOpenMode(const adios2_mode modeC)
{
    adios2::Mode mode = adios2::Mode::Undefined;
    switch (modeC)
    {

    case adios2_mode_write:
        mode = adios2::Mode::Write;
        break;

    case adios2_mode_read:
        mode = adios2::Mode::Read;
        break;

    case adios2_mode_append:
        mode = adios2::Mode::Append;
        break;

    default:
        break;
    }
    return mode;
}
} // end anonymous namespace

adios2_engine *adios2_open(adios2_io *io, const char *name,
                           const adios2_mode mode)
{
    adios2_engine *engine = nullptr;
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_open");
        engine = reinterpret_cast<adios2_engine *>(
            &reinterpret_cast<adios2::core::IO *>(io)->Open(
                name, adios2_ToOpenMode(mode)));
    }
    catch (...)
    {
        adios2::helper::ExceptionToError("adios2_open");
    }
    return engine;
}

#ifdef ADIOS2_HAVE_MPI
adios2_engine *adios2_open_new_comm(adios2_io *io, const char *name,
                                    const adios2_mode mode, MPI_Comm comm)
{
    adios2_engine *engine = nullptr;
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_open");
        engine = reinterpret_cast<adios2_engine *>(
            &reinterpret_cast<adios2::core::IO *>(io)->Open(
                name, adios2_ToOpenMode(mode), comm));
    }
    catch (...)
    {
        adios2::helper::ExceptionToError("adios2_open_new_comm");
    }
    return engine;
}
#endif

adios2_error adios2_flush_all_engines(adios2_io *io)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_flush_all_engines");
        reinterpret_cast<adios2::core::IO *>(io)->FlushAll();
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_flush_all_engines"));
    }
}

adios2_error adios2_engine_type(char *engine_type, size_t *size,
                                const adios2_io *io)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for const adios2_io, in call to adios2_io_engine_type");
        adios2::helper::CheckForNullptr(
            engine_type,
            "for char* engine_type, in call to adios2_io_engine_type");
        adios2::helper::CheckForNullptr(
            size, "for size_t* size, in call to adios2_io_engine_type");

        const adios2::core::IO *ioCpp =
            reinterpret_cast<const adios2::core::IO *>(io);

        *size = ioCpp->m_EngineType.size();
        ioCpp->m_EngineType.copy(engine_type, *size);
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_engine_type"));
    }
}

adios2_error adios2_lock_definitions(adios2_io *io)
{
    try
    {
        adios2::helper::CheckForNullptr(
            io, "for adios2_io, in call to adios2_lock_definitions");
        reinterpret_cast<adios2::core::IO *>(io)->LockDefinitions();
        return adios2_error_none;
    }
    catch (...)
    {
        return static_cast<adios2_error>(
            adios2::helper::ExceptionToError("adios2_engine_type"));
    }
}

#ifdef __cplusplus
} // end extern C
#endif
