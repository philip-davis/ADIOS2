/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * WANZmq.cpp
 *
 *  Created on: May 26, 2017
 *      Author: Jason Wang wangr1@ornl.gov
 */

#include "WANZmq.h"

#include <iostream>
#include <zmq.h>

namespace adios2
{
namespace transport
{

WANZmq::WANZmq(const std::string ipAddress, const std::string port,
               MPI_Comm mpiComm, const bool debugMode)
: Transport("wan", "zmq", mpiComm, debugMode), m_IPAddress(ipAddress),
  m_Port(port)
{
    m_Context = zmq_ctx_new();
    if (m_Context == nullptr || m_Context == NULL)
    {
        throw std::runtime_error("ERROR: Creating ZeroMQ context failed");
    }
    if (m_DebugMode)
    {
        std::cout << "[WANZmq] IP Address " << ipAddress << std::endl;
        std::cout << "[WANZmq] Port " << port << std::endl;
    }
}

WANZmq::~WANZmq()
{
    if (m_Socket)
    {
        zmq_close(m_Socket);
    }
    if (m_Context)
    {
        zmq_ctx_destroy(m_Context);
    }
}

void WANZmq::Open(const std::string &name, const Mode openMode)
{
    m_Name = name;
    m_OpenMode = openMode;

    if (m_OpenMode == Mode::Write)
    {
        if (m_Profiler.IsActive)
        {
            m_Profiler.Timers.at("open").Resume();
        }

        m_Socket = zmq_socket(m_Context, ZMQ_REQ);
        const std::string fullIP("tcp://" + m_IPAddress + ":" + m_Port);
        int err = zmq_connect(m_Socket, fullIP.c_str());
        if (err)
        {
            throw std::runtime_error("ERROR: zmq_connect() failed with " +
                                     std::to_string(err));
        }

        if (m_Profiler.IsActive)
        {
            m_Profiler.Timers.at("open").Pause();
        }
        if (m_DebugMode)
        {
            std::cout << "[WANZmq] Open Mode Write" << std::endl;
        }
    }

    else if (m_OpenMode == Mode::Append)
    {
        if (m_DebugMode)
        {
            std::cout << "[WANZmq] Open Mode Append" << std::endl;
            throw std::invalid_argument(
                "ERROR: WAN transport " + m_Name +
                " only supports "
                "OpenMode:w (write/sender) and "
                "OpenMode:r (read/receiver), in call to Open\n");
        }
    }
    else if (m_OpenMode == Mode::Read)
    {
        ProfilerStart("open");
        m_Socket = zmq_socket(m_Context, ZMQ_REP);
        const std::string fullIP("tcp://" + m_IPAddress + ":" + m_Port);
        zmq_bind(m_Socket, fullIP.c_str());
        // TODO need to capture return of zmq_bind function
        ProfilerStop("open");
        if (m_DebugMode)
        {
            std::cout << "[WANZmq] Open Mode Read" << std::endl;
        }
    }

    if (m_DebugMode)
    {
        if (m_Socket == nullptr || m_Socket == NULL) // something goes wrong
        {
            throw std::ios_base::failure(
                "ERROR: couldn't open socket for address " + m_Name +
                ", in call to WANZmq Open\n");
        }
    }
    m_IsOpen = true;
}

void WANZmq::SetBuffer(char *buffer, size_t size) {}

void WANZmq::Write(const char *buffer, size_t size, size_t start)
{
    ProfilerStart("write");
    const int status = zmq_send(m_Socket, buffer, size, 0);
    char ret[10];
    zmq_recv(m_Socket, ret, 10, 0);
    ProfilerStop("write");

    const std::string retString(ret);

    if (status == -1 || retString != "OK")
    {
        throw std::ios_base::failure("ERROR: couldn't send message " + m_Name +
                                     ", in call to WANZmq write\n");
    }
}

void WANZmq::IWrite(const char *buffer, size_t size, Status &status,
                    size_t start)
{
}

void WANZmq::Read(char *buffer, size_t size, size_t start)
{
    zmq_recv(m_Socket, buffer, size, 0);
    int status = zmq_send(m_Socket, "OK", 4, 0);
}

void WANZmq::IRead(char *buffer, size_t size, Status &status, size_t start)
{
    int bytes = zmq_recv(m_Socket, buffer, size, ZMQ_DONTWAIT);
    int ret = zmq_send(m_Socket, "OK", 4, 0);
    if (bytes > 0)
    {
        status.Bytes = bytes;
        status.Running = true;
    }
    else
    {
        status.Bytes = 0;
        status.Running = true;
    }
    if (bytes == size)
    {
        status.Successful = true;
    }
    else
    {
        status.Successful = false;
    }
}

void WANZmq::Flush() {}

void WANZmq::Close()
{
    if (m_Socket != nullptr)
    {
        zmq_close(m_Socket);
    }
}

} // end namespace transport
} // end namespace adios2
